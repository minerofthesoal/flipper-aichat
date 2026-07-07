from __future__ import annotations

import asyncio

import httpx
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical
from textual.widgets import (
    Header,
    Footer,
    Input,
    RichLog,
    Select,
    Static,
    Button,
)
from textual.reactive import reactive

from .config import Config
from .lmstudio_client import LmStudioClient
from .serial_link import SerialLink

MODE_DIRECT = "direct"
MODE_SERIAL = "serial"


class StatusBar(Static):
    mode: reactive[str] = reactive(MODE_DIRECT)
    model: reactive[str] = reactive("auto")
    connection: reactive[str] = reactive("disconnected")

    def render(self) -> str:
        return (
            f"[b]mode:[/b] {self.mode}   "
            f"[b]model:[/b] {self.model}   "
            f"[b]link:[/b] {self.connection}"
        )


class AiChatTuiApp(App):
    """Linux companion TUI for the Flipper AI Chat app.

    Two modes:
      * Direct  - talk straight to LM Studio over Tailscale (fast iteration,
                  no Flipper/devboard needed).
      * Serial  - talk to the ESP32 devboard over USB, exactly like the
                  Flipper does. Useful for provisioning WiFi/server config
                  and for debugging the bridge firmware.
    """

    CSS = """
    Screen {
        layout: vertical;
    }
    #main {
        height: 1fr;
    }
    #sidebar {
        width: 28;
        border: round $accent;
        padding: 1;
    }
    #chat-log {
        border: round $accent;
    }
    StatusBar {
        background: $panel;
        padding: 0 1;
        height: 1;
    }
    #input-row {
        height: 3;
    }
    """

    BINDINGS = [
        ("ctrl+m", "toggle_mode", "Toggle Direct/Serial"),
        ("ctrl+r", "refresh_models", "Refresh models"),
        ("ctrl+u", "edit_server_url", "Set LM Studio host"),
        ("ctrl+q", "quit", "Quit"),
        # Textual puts the terminal in raw mode, which disables the normal
        # tty behavior where Ctrl+C sends SIGINT - without this, Ctrl+C just
        # arrives as an ordinary keypress and does nothing, which looks like
        # a hang to anyone (reasonably) expecting Ctrl+C to always work.
        # Hidden from the footer so it doesn't duplicate the Ctrl+Q hint.
        Binding("ctrl+c", "quit", "Quit", show=False),
    ]

    def __init__(self):
        super().__init__()
        self.cfg = Config.load()
        self.mode = MODE_DIRECT
        self.models: list[str] = []
        self.serial_link: SerialLink | None = None
        self.lm_client = LmStudioClient(self.cfg.lmstudio_url)
        self.history: list[dict] = []
        self._streaming_task: asyncio.Task | None = None

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        yield StatusBar(id="status")
        with Horizontal(id="main"):
            yield RichLog(id="chat-log", wrap=True, highlight=True, markup=True)
            with Vertical(id="sidebar"):
                yield Static("Model")
                yield Select([], id="model-select", allow_blank=True)
                yield Button("Refresh models", id="refresh-models")
                yield Static("")
                yield Static("LM Studio host (Ctrl+U, Enter to apply)")
                yield Input(value=self.cfg.lmstudio_url, placeholder="http://host:port", id="server-url-input")
                yield Static(f"Serial port:\n{self.cfg.serial_port}")
        with Horizontal(id="input-row"):
            yield Input(placeholder="Type a message and press Enter to send...", id="chat-input")
        yield Footer()

    async def on_mount(self) -> None:
        status = self.query_one(StatusBar)
        status.mode = self.mode
        self.action_refresh_models()
        self.query_one("#chat-input", Input).focus()

    def action_toggle_mode(self) -> None:
        self.mode = MODE_SERIAL if self.mode == MODE_DIRECT else MODE_DIRECT
        status = self.query_one(StatusBar)
        status.mode = self.mode
        log = self.query_one("#chat-log", RichLog)

        if self.mode == MODE_SERIAL:
            try:
                self.serial_link = SerialLink(
                    self.cfg.serial_port, self.cfg.serial_baud, asyncio.get_running_loop()
                )
                self.serial_link.open()
                self.run_worker(self._pump_serial(), exclusive=False)
                status.connection = f"serial:{self.cfg.serial_port}"
                log.write("[green]Switched to Serial mode - talking to the devboard over USB.[/]")
            except Exception as e:  # noqa: BLE001
                log.write(f"[red]Failed to open serial port: {e}[/]")
                self.mode = MODE_DIRECT
                status.mode = self.mode
        else:
            if self.serial_link:
                self.serial_link.close()
                self.serial_link = None
            status.connection = "direct"
            log.write("[green]Switched to Direct mode - talking to LM Studio directly.[/]")

    async def _pump_serial(self) -> None:
        assert self.serial_link is not None
        log = self.query_one("#chat-log", RichLog)
        link = self.serial_link
        while self.mode == MODE_SERIAL and self.serial_link is link:
            msg = await link.queue.get()
            if msg.type == "token":
                log.write(msg.payload, end="")
            elif msg.type == "done":
                log.write("")
            elif msg.type == "error":
                log.write(f"[red]{msg.payload}[/]")
            elif msg.type == "models":
                self.models = [m for m in msg.payload.split(",") if m]
                self._refresh_model_select()
            elif msg.type == "info":
                self.query_one(StatusBar).connection = msg.payload
            else:
                log.write(f"[dim]{msg.payload}[/]")

    def action_refresh_models(self) -> None:
        # Always a worker, never a direct await from a message handler: a
        # bad host used to hang the *button press itself* for up to the
        # full 60s timeout, which is what looked like a freeze. Running it
        # as an exclusive worker also means mashing Ctrl+R again (e.g.
        # after fixing the host) cancels the stuck attempt and starts a
        # fresh one immediately, instead of queueing behind it.
        self.run_worker(self._refresh_models_worker(), exclusive=True, group="refresh")

    async def _refresh_models_worker(self) -> None:
        log = self.query_one("#chat-log", RichLog)
        status = self.query_one(StatusBar)
        if self.mode == MODE_DIRECT:
            status.connection = f"connecting to {self.cfg.lmstudio_url}..."
            try:
                self.models = await self.lm_client.list_models()
                self._refresh_model_select()
                status.connection = "direct"
            except httpx.ConnectError as e:
                status.connection = "error"
                log.write(
                    f"[red]Could not reach LM Studio at {self.cfg.lmstudio_url} "
                    f"({e}). Check the host in the sidebar (Ctrl+U) and press "
                    f"Ctrl+R to retry.[/]"
                )
            except httpx.TimeoutException:
                status.connection = "error"
                log.write(
                    f"[red]Timed out reaching LM Studio at {self.cfg.lmstudio_url}. "
                    f"Check the host in the sidebar (Ctrl+U) and press Ctrl+R to retry.[/]"
                )
            except Exception as e:  # noqa: BLE001
                status.connection = "error"
                log.write(f"[red]Could not reach LM Studio: {e}[/]")
        elif self.serial_link:
            self.serial_link.send("LISTMODELS")

    def action_edit_server_url(self) -> None:
        self.query_one("#server-url-input", Input).focus()

    async def _apply_server_url(self, url: str) -> None:
        log = self.query_one("#chat-log", RichLog)
        field = self.query_one("#server-url-input", Input)
        if not url:
            log.write("[red]Server URL can't be empty.[/]")
            field.value = self.cfg.lmstudio_url
            return
        self.cfg.lmstudio_url = url
        self.cfg.save()
        self.lm_client = LmStudioClient(url)
        field.value = url
        log.write(f"[green]LM Studio host set to {url}[/]")
        if self.mode == MODE_DIRECT:
            self.action_refresh_models()

    def _refresh_model_select(self) -> None:
        select = self.query_one("#model-select", Select)
        select.set_options([(m, m) for m in self.models])
        if self.models and select.value == Select.BLANK:
            select.value = self.models[0]
            self.cfg.last_model = self.models[0]

    async def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "refresh-models":
            self.action_refresh_models()

    def on_select_changed(self, event: Select.Changed) -> None:
        if event.select.id == "model-select" and event.value != Select.BLANK:
            self.cfg.last_model = str(event.value)
            self.query_one(StatusBar).model = str(event.value)
            if self.mode == MODE_SERIAL and self.serial_link:
                self.serial_link.send_select_model(str(event.value))

    async def on_input_submitted(self, event: Input.Submitted) -> None:
        if event.input.id == "server-url-input":
            await self._apply_server_url(event.value.strip())
            return

        text = event.value.strip()
        if not text:
            return
        event.input.value = ""
        log = self.query_one("#chat-log", RichLog)
        log.write(f"[b cyan]You:[/] {text}")

        if self.mode == MODE_SERIAL:
            if self.serial_link:
                self.serial_link.send_chat(text)
            else:
                log.write("[red]Not connected to devboard.[/]")
            return

        self.history.append({"role": "user", "content": text})
        log.write("[b magenta]AI:[/] ", end="")
        self.run_worker(self._stream_direct(), exclusive=True, group="stream")

    async def _stream_direct(self) -> None:
        log = self.query_one("#chat-log", RichLog)
        model = self.cfg.last_model or "auto"
        reply = ""
        try:
            async for delta in self.lm_client.stream_chat(model, self.history):
                reply += delta
                log.write(delta, end="")
        except Exception as e:  # noqa: BLE001
            log.write(f"\n[red]Error: {e}[/]")
            return
        log.write("")
        self.history.append({"role": "assistant", "content": reply})

    def on_unmount(self) -> None:
        self.cfg.save()
        if self.serial_link:
            self.serial_link.close()


def run() -> None:
    AiChatTuiApp().run()


if __name__ == "__main__":
    run()

"""Textual runs the terminal in raw mode, which disables the tty's normal
Ctrl+C -> SIGINT behavior - inside the app, Ctrl+C is just an ordinary
keypress and does nothing unless a binding says otherwise. This regression-
tests the fix for exactly that: Ctrl+C silently doing nothing while the app
was Direct-mode-startup-slow enough to look hung.

Also covers the "Refresh models freezes the TUI" fix: action_refresh_models
used to be an `async def` awaited directly from on_mount/on_button_pressed,
so a bad LM Studio host blocked that handler for up to the full timeout.
It's now a plain sync function that hands the network call off to a
cancellable worker - plus the LM Studio host is now editable from the
sidebar instead of requiring a config file edit + restart.
"""

from __future__ import annotations

import asyncio
import inspect

from ai_chat_tui.app import AiChatTuiApp


def _resolve_action(app: AiChatTuiApp, key: str) -> str | None:
    bindings = app._bindings.key_to_bindings.get(key)
    if not bindings:
        return None
    return bindings[0].action


def test_ctrl_c_quits():
    app = AiChatTuiApp()
    assert _resolve_action(app, "ctrl+c") == "quit"


def test_ctrl_q_quits():
    """The documented quit key shown in the footer - make sure adding the
    Ctrl+C alias didn't accidentally replace it."""
    app = AiChatTuiApp()
    assert _resolve_action(app, "ctrl+q") == "quit"


def test_ctrl_c_hidden_from_footer():
    """Ctrl+C is a safety-net alias, not the documented shortcut - it
    shouldn't duplicate the Ctrl+Q hint in the footer."""
    app = AiChatTuiApp()
    binding = app._bindings.key_to_bindings["ctrl+c"][0]
    assert binding.show is False


def test_refresh_models_action_is_not_a_blocking_coroutine():
    """The actual freeze fix: this must be a plain sync function (that
    internally starts a worker) rather than something callers await
    directly, or a bad host goes right back to hanging the caller."""
    assert not inspect.iscoroutinefunction(AiChatTuiApp.action_refresh_models)


def test_ctrl_u_binding_focuses_server_url_field():
    app = AiChatTuiApp()
    assert _resolve_action(app, "ctrl+u") == "edit_server_url"


def test_app_mounts_without_error():
    """Smoke test: build and actually mount the app (via Textual's own
    run_test() harness) and make sure nothing raises - catching import/
    wiring mistakes (like the Binding import needed for the Ctrl+C fix
    above) before they reach a real terminal.

    on_mount() normally calls out to LM Studio for a model list; the default
    config points at a placeholder Tailscale IP (100.64.0.1) that's
    unroutable from a CI runner, which would otherwise make this test wait
    out the client's connect timeout. That network path is already covered
    by the LmStudioClient tests, so it's stubbed out here to keep this test
    fast and focused on "does the app mount cleanly".
    """

    async def _run():
        app = AiChatTuiApp()
        app.action_refresh_models = lambda: None
        async with app.run_test():
            assert app.query_one("#chat-log") is not None
            assert app.query_one("#chat-input") is not None
            assert app.query_one("#server-url-input") is not None

    asyncio.run(_run())


def test_server_url_field_prefilled_from_config():
    async def _run():
        app = AiChatTuiApp()
        app.action_refresh_models = lambda: None
        async with app.run_test():
            from textual.widgets import Input

            field = app.query_one("#server-url-input", Input)
            assert field.value == app.cfg.lmstudio_url

    asyncio.run(_run())


def test_apply_server_url_updates_config_and_client_without_touching_chat():
    """This is the actual feature: changing the host in the sidebar should
    update the running client and persist to config, and must NOT be
    treated as a chat message (a naive Input.Submitted handler that doesn't
    check event.input.id would send it straight to the LLM)."""

    async def _run():
        app = AiChatTuiApp()
        app.action_refresh_models = lambda: None
        async with app.run_test():
            old_client = app.lm_client
            await app._apply_server_url("http://192.168.1.50:1234")
            assert app.cfg.lmstudio_url == "http://192.168.1.50:1234"
            assert app.lm_client is not old_client
            assert app.lm_client.base_url == "http://192.168.1.50:1234"
            assert app.history == []  # never went through the chat-send path

    asyncio.run(_run())

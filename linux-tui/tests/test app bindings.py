"""Textual runs the terminal in raw mode, which disables the tty's normal
Ctrl+C -> SIGINT behavior - inside the app, Ctrl+C is just an ordinary
keypress and does nothing unless a binding says otherwise. This regression-
tests the fix for exactly that: Ctrl+C silently doing nothing while the app
was Direct-mode-startup-slow enough to look hung.
"""

from __future__ import annotations

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


def test_app_mounts_without_error():
    """Smoke test: build and actually mount the app (via Textual's own
    run_test() harness) and make sure nothing raises - catching import/
    wiring mistakes (like the Binding import needed for the Ctrl+C fix
    above) before they reach a real terminal.

    on_mount() normally calls out to LM Studio for a model list; the default
    config points at a placeholder Tailscale IP (100.64.0.1) that's
    unroutable from a CI runner, which would otherwise make this test hang
    for the client's full connect timeout. That network path is already
    covered by the LmStudioClient tests, so it's stubbed out here to keep
    this test fast and focused on "does the app mount cleanly".
    """
    import asyncio

    async def _run():
        app = AiChatTuiApp()
        app.action_refresh_models = lambda: asyncio.sleep(0)
        async with app.run_test():
            assert app.query_one("#chat-log") is not None
            assert app.query_one("#chat-input") is not None

    asyncio.run(_run())

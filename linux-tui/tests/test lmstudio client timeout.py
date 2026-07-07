"""LmStudioClient used to use one timeout value for everything, including
the initial TCP connect. That meant a wrong/unreachable host (the actual
bug report: "trying to connect to the wrong host") took just as long to
fail as we're willing to wait for a slow model to finish generating - up
to 60s by default, which from the TUI just looked like a freeze.

These are narrow config-level tests; the *behavioral* side (a bad host
actually failing fast) is covered by test_long_range_connection.py, which
exercises real sockets.
"""

from __future__ import annotations

from ai_chat_tui.lmstudio_client import LmStudioClient


def test_connect_timeout_is_much_shorter_than_read_timeout_by_default():
    client = LmStudioClient("http://100.64.0.1:1234")
    assert client.timeout.connect == 5.0
    assert client.timeout.read == 60.0


def test_connect_timeout_independently_overridable():
    client = LmStudioClient("http://100.64.0.1:1234", timeout=10.0, connect_timeout=2.0)
    assert client.timeout.connect == 2.0
    # Overriding connect_timeout shouldn't affect the read timeout used
    # once a connection is actually established (e.g. waiting for a slow
    # model to generate tokens).
    assert client.timeout.read == 10.0

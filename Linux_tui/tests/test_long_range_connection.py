"""Emulates a "long range" connection - the kind of link you actually get
when LM Studio is reached through a Tailscale DERP relay, a mobile hotspot,
or just a physically distant tailnet peer, instead of the same LAN. That
means: multi-hundred-ms round trips, response bytes trickling in a few at a
time instead of arriving in one TCP segment, and occasional multi-second
stalls mid-stream.

None of this requires a real network - a local HTTP server deliberately
slowed down with `time.sleep()` reproduces the same client-visible symptoms
(the client never knows *why* a socket read is slow), which is what
LmStudioClient - and by extension the ESP32 bridge, whose handleChat() is
lifted straight from the same OpenAI streaming contract - actually has to
cope with.
"""

from __future__ import annotations

import asyncio
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import httpx
import pytest

from ai_chat_tui.lmstudio_client import LmStudioClient

MODELS_BODY = json.dumps({"data": [{"id": "qwen2.5-7b"}, {"id": "llama-3.1-8b"}]}).encode()


def _sse_chunk(text: str) -> bytes:
    payload = json.dumps({"choices": [{"delta": {"content": text}}]})
    return f"data: {payload}\n\n".encode()


class _SlowLinkHandler(BaseHTTPRequestHandler):
    # Test knobs, overridden per-server-instance via the factory below.
    connect_delay = 0.0  # delay before the first byte goes out (RTT stand-in)
    chunk_delay = 0.0  # delay between each SSE token
    trickle = False  # if True, write the body one byte at a time
    stall_after_chunk = None  # index after which to hang forever (timeout test)
    tokens = ("Hel", "lo ", "the", "re!")

    def log_message(self, *_args):  # noqa: D401 - silence default stderr spam
        pass

    def _write_slow(self, data: bytes):
        if not self.trickle:
            self.wfile.write(data)
            self.wfile.flush()
            return
        for i in range(len(data)):
            self.wfile.write(data[i : i + 1])
            self.wfile.flush()

    def do_GET(self):
        if self.path == "/v1/models":
            import time

            time.sleep(self.connect_delay)
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(MODELS_BODY)))
            self.end_headers()
            self._write_slow(MODELS_BODY)

    def do_POST(self):
        import time

        if self.path != "/v1/chat/completions":
            self.send_response(404)
            self.end_headers()
            return

        # Drain the request body so the client's write isn't left hanging.
        length = int(self.headers.get("Content-Length", 0))
        if length:
            self.rfile.read(length)

        time.sleep(self.connect_delay)
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        # Deliberately no Content-Length: this is close-delimited, exactly
        # like LM Studio's real streaming responses.
        self.end_headers()
        self.close_connection = True

        for i, tok in enumerate(self.tokens):
            if self.stall_after_chunk is not None and i == self.stall_after_chunk:
                time.sleep(10)  # far longer than any test's client timeout
            self._write_slow(_sse_chunk(tok))
            time.sleep(self.chunk_delay)
        self._write_slow(b"data: [DONE]\n\n")


def _make_server(**handler_kwargs) -> tuple[ThreadingHTTPServer, str]:
    handler_cls = type("_ConfiguredHandler", (_SlowLinkHandler,), handler_kwargs)
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler_cls)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base_url = f"http://127.0.0.1:{server.server_address[1]}"
    return server, base_url


def test_list_models_survives_high_latency():
    """A DERP-relayed or long-haul tailnet link can easily add 1-2s of RTT
    on top of LAN latency. The client's timeout needs to comfortably cover
    that or every model refresh after a Tailscale reconnect will error out."""
    server, base_url = _make_server(connect_delay=1.5)
    try:
        client = LmStudioClient(base_url, timeout=5.0)
        models = asyncio.run(client.list_models())
        assert models == ["qwen2.5-7b", "llama-3.1-8b"]
    finally:
        server.shutdown()


def test_stream_chat_with_trickling_bytes():
    """Long-range paths often fragment responses into many small TCP
    segments (lower effective MTU through relays/tunnels). The SSE line
    parser has to reassemble those correctly regardless of how the bytes
    were chopped up on the wire."""
    server, base_url = _make_server(trickle=True, chunk_delay=0.01)
    try:
        client = LmStudioClient(base_url, timeout=5.0)

        async def collect():
            out = []
            async for delta in client.stream_chat("qwen2.5-7b", [{"role": "user", "content": "hi"}]):
                out.append(delta)
            return out

        deltas = asyncio.run(collect())
        assert "".join(deltas) == "Hello there!"
    finally:
        server.shutdown()


def test_stream_chat_survives_slow_but_steady_link():
    """Every chunk arrives, just slowly (e.g. ~1s spacing) - should still
    complete as long as no single gap exceeds the client's timeout."""
    server, base_url = _make_server(chunk_delay=0.5)
    try:
        client = LmStudioClient(base_url, timeout=5.0)

        async def collect():
            out = []
            async for delta in client.stream_chat("qwen2.5-7b", [{"role": "user", "content": "hi"}]):
                out.append(delta)
            return out

        deltas = asyncio.run(collect())
        assert "".join(deltas) == "Hello there!"
    finally:
        server.shutdown()


def test_stream_chat_times_out_on_a_stalled_link():
    """If the link drops mid-stream (common with a flaky relay hop) the
    client must raise rather than hang indefinitely - this is the same
    "idle too long -> bail" contract the ESP32 bridge implements with
    HTTP_TIMEOUT_MS around its SSE read loop."""
    server, base_url = _make_server(stall_after_chunk=1)
    try:
        client = LmStudioClient(base_url, timeout=1.0)

        async def collect():
            out = []
            async for delta in client.stream_chat("qwen2.5-7b", [{"role": "user", "content": "hi"}]):
                out.append(delta)
            return out

        with pytest.raises(httpx.ReadTimeout):
            asyncio.run(collect())
    finally:
        server.shutdown()

from __future__ import annotations

import asyncio
import threading
from typing import Callable

import serial

from . import protocol


class SerialLink:
    """Runs a background thread reading lines from the ESP32 bridge over
    USB serial, and hands parsed protocol.Msg objects to an asyncio queue so
    the Textual app can await them normally."""

    def __init__(self, port: str, baud: int, loop: asyncio.AbstractEventLoop):
        self.port = port
        self.baud = baud
        self.loop = loop
        self.queue: asyncio.Queue[protocol.Msg] = asyncio.Queue()
        self._ser: serial.Serial | None = None
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()

    def open(self) -> None:
        self._ser = serial.Serial(self.port, self.baud, timeout=0.2)
        self._stop.clear()
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=1)
        if self._ser:
            self._ser.close()

    def _read_loop(self) -> None:
        buf = b""
        while not self._stop.is_set() and self._ser:
            try:
                chunk = self._ser.read(256)
            except serial.SerialException:
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", errors="replace")
                msg = protocol.parse(text)
                asyncio.run_coroutine_threadsafe(self.queue.put(msg), self.loop)

    def send(self, line: str) -> None:
        if self._ser:
            self._ser.write((line + "\n").encode("utf-8"))

    def send_chat(self, text: str) -> None:
        self.send(protocol.build_chat(text))

    def send_select_model(self, name: str) -> None:
        self.send(protocol.build_select_model(name))

    def send_set_wifi(self, ssid: str, password: str) -> None:
        self.send(protocol.build_set_wifi(ssid, password))

    def send_set_server(self, url: str) -> None:
        self.send(protocol.build_set_server(url))

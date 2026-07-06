"""
Mirrors flipper-app/ai_chat_protocol.c - see docs/PROTOCOL.md for the spec.
Used by the TUI's Serial mode when talking straight to the ESP32 bridge
(e.g. for provisioning WiFi/server config, or testing without a Flipper).
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass
class Msg:
    type: str  # "token" | "done" | "error" | "models" | "info" | "unknown"
    payload: str


def parse(line: str) -> Msg:
    line = line.rstrip("\r\n")
    if "|" in line:
        tag, rest = line.split("|", 1)
    else:
        tag, rest = line, ""

    if tag == "TOK":
        return Msg("token", rest)
    if tag == "DONE":
        return Msg("done", "")
    if tag == "ERR":
        return Msg("error", rest)
    if tag == "MODELS":
        return Msg("models", rest)
    if tag == "INFO":
        return Msg("info", rest)
    return Msg("unknown", line)


def build_chat(text: str) -> str:
    return f"CHAT|{text}"


def build_select_model(name: str) -> str:
    return f"SELMODEL|{name}"


def build_set_wifi(ssid: str, password: str) -> str:
    return f"SETWIFI|{ssid}|{password}"


def build_set_server(url: str) -> str:
    return f"SETSERVER|{url}"

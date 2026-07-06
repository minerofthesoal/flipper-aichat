from __future__ import annotations

import tomllib
import tomli_w  # type: ignore[import-not-found]
from dataclasses import dataclass, asdict
from pathlib import Path

CONFIG_DIR = Path.home() / ".config" / "ai-chat-tui"
CONFIG_PATH = CONFIG_DIR / "config.toml"


@dataclass
class Config:
    lmstudio_url: str = "http://100.64.0.1:1234"  # your Tailscale tailnet IP + LM Studio port
    serial_port: str = "/dev/ttyACM0"
    serial_baud: int = 115200
    last_model: str = "auto"

    @classmethod
    def load(cls) -> "Config":
        if not CONFIG_PATH.exists():
            cfg = cls()
            cfg.save()
            return cfg
        with open(CONFIG_PATH, "rb") as f:
            data = tomllib.load(f)
        return cls(**{**asdict(cls()), **data})

    def save(self) -> None:
        CONFIG_DIR.mkdir(parents=True, exist_ok=True)
        with open(CONFIG_PATH, "wb") as f:
            tomli_w.dump(asdict(self), f)

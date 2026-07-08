from __future__ import annotations

import json
from typing import AsyncIterator

import httpx


class LmStudioClient:
    """Talks directly to LM Studio's OpenAI-compatible API - used by the
    TUI's Direct mode so you can test prompts/models against your home PC
    over Tailscale without a Flipper attached at all."""

    def __init__(self, base_url: str, timeout: float = 60.0):
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    async def list_models(self) -> list[str]:
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.get(f"{self.base_url}/v1/models")
            resp.raise_for_status()
            data = resp.json()
            return [item["id"] for item in data.get("data", [])]

    async def stream_chat(self, model: str, messages: list[dict]) -> AsyncIterator[str]:
        """Yields text deltas as they arrive; raises on connection errors."""
        payload = {"model": model, "messages": messages, "stream": True}
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            async with client.stream(
                "POST", f"{self.base_url}/v1/chat/completions", json=payload
            ) as resp:
                resp.raise_for_status()
                async for line in resp.aiter_lines():
                    if not line.startswith("data: "):
                        continue
                    data = line[len("data: "):].strip()
                    if data == "[DONE]":
                        return
                    try:
                        chunk = json.loads(data)
                    except json.JSONDecodeError:
                        continue
                    delta = chunk.get("choices", [{}])[0].get("delta", {}).get("content")
                    if delta:
                        yield delta

# Alternative transport: phone as the Tailscale<->BLE relay

The primary design (devboard's ESP32 joins WiFi, talks to LM Studio through
`tailscale serve`/`funnel`) is what's implemented in `esp32-bridge/`. This
document covers the alternative you asked about: using your phone, already
on your tailnet via the Tailscale app, as the relay instead - with the
Flipper talking to the phone over BLE rather than the devboard talking WiFi.

**Honest scope note:** this path is materially more work than the WiFi
route, because it needs your phone to act as a *BLE GATT peripheral*
(advertising a service the Flipper connects to), and neither Android nor iOS
expose that capability to Termux/shell scripts - it requires a real native
or React Native app with BLE peripheral permissions. That's a separate app
project on top of everything here, not a config tweak. This repo does not
include that app. What follows is the design plus a runnable stub for the
one part that *is* scriptable today (the Tailscale-facing half), so you have
a running start if you decide to build it.

## Design

```
Flipper (GATT client) --BLE--> Phone app (GATT server + Tailscale) --HTTP--> LM Studio (Tailscale)
```

- The phone app would expose a custom BLE GATT service with two
  characteristics: one the Flipper writes chat text to, one it subscribes to
  for streamed token notifications - mirroring the same pipe-delimited
  protocol used over UART (`docs/PROTOCOL.md`), just carried over BLE
  notify/write instead of serial bytes.
- On the phone side, since the Tailscale app already puts the phone on your
  tailnet, the relay code just needs to make normal HTTP requests to your PC's
  tailnet address - no different from what the ESP32 firmware does.
- Practical starting points for the native half: Android (Kotlin,
  `BluetoothGattServer`) or a cross-platform BLE peripheral library. iOS
  peripheral mode exists (`CoreBluetooth` `CBPeripheralManager`) but has
  stricter background limits.

## Runnable stub: the Tailscale-facing half

If you do build the native BLE half, it can shell out to (or reimplement)
this logic. This is the same protocol handling used by `esp32-bridge/`,
factored out so it's portable:

```python
# phone_relay_stub.py - the non-BLE half: given plain text in, stream tokens
# out. A native BLE layer wires its GATT write callback to on_chat(), and
# forwards on_token() calls to a notify().
import asyncio
from ai_chat_tui.lmstudio_client import LmStudioClient  # reuse from linux-tui/

client = LmStudioClient(base_url="http://100.x.x.x:1234")  # your PC's tailnet IP

async def on_chat(text: str, on_token, on_done):
    async for delta in client.stream_chat("auto", [{"role": "user", "content": text}]):
        on_token(delta)
    on_done()
```

If/when you want to take this further, it's worth prototyping the BLE
peripheral side first in isolation (just advertise + echo) before wiring in
the network half - that's the part with real platform-specific gotchas.

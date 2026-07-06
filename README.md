# Flipper AI Chat

Chat with an LLM running on your home PC (LM Studio), from your Flipper
Zero, using the WiFi Devboard as the network bridge. Includes a Linux TUI
companion for direct testing and for provisioning the devboard.

```
┌───────────┐   UART 115200   ┌────────────────┐   WiFi    ┌──────────────┐
│ Flipper   │ <-------------> │ ESP32-S2       │ <-------> │ LM Studio    │
│ Zero      │  pins 13/14     │ WiFi Devboard  │  HTTP     │ on your PC   │
│ (this app)│                 │ (bridge fw)    │           │ (+ Tailscale │
└───────────┘                 └────────────────┘           │  for remote) │
                                                             └──────────────┘

              ┌──────────────┐
              │ Linux TUI    │──── Direct mode ────> LM Studio directly
              │ (this repo)  │──── Serial mode ────> ESP32 devboard over USB
              └──────────────┘      (for provisioning/testing)
```

## Components

| Path              | What it is                                                          |
|-------------------|----------------------------------------------------------------------|
| `flipper-app/`    | The `.fap` app: chat UI, on-demand keyboard, streaming, model select |
| `esp32-bridge/`   | Firmware for the WiFi Devboard: UART <-> LM Studio HTTP bridge        |
| `linux-tui/`      | Textual TUI: direct LM Studio chat, or serial link to the devboard    |
| `docs/PROTOCOL.md`| The Flipper<->devboard wire protocol                                  |
| `docs/SETUP.md`   | Step-by-step setup (LM Studio, Tailscale, flashing, installing)       |
| `docs/PHONE_RELAY.md` | Design + starter stub for the phone/BLE alternative transport     |
| `.github/workflows/ci.yml` | One workflow: tests + builds both firmwares + tags a release |

## Flipper app UI

- **Chat view** (default screen): scrolling history, bottom-anchored, Up/Down
  to scroll back through it.
- Press **OK** to open the keyboard. It only appears while composing - the
  chat view is what you see otherwise, so there's no keyboard cluttering the
  screen at rest.
- The keyboard's confirm action **is** the send button - finishing text entry
  sends the message and drops you back to the chat view, where the reply
  streams in token-by-token.
- Press **Left** from the chat view to open **model selection** (optional -
  works with whatever LM Studio reports it has loaded).
- **Back** exits the app.

## Why this transport design

The ESP32 on the Devboard can't run a Tailscale client itself, so it can't
directly join your tailnet. The bridge firmware instead just makes a plain
HTTP request to wherever you point it - which in practice means your PC's
LAN IP at home, or a `tailscale serve` address if you want the *Linux TUI*
(not the Flipper) to reach the same LM Studio instance while you're out.
See `docs/SETUP.md` for the concrete addressing options, and
`docs/PHONE_RELAY.md` for the phone-as-relay alternative you mentioned,
including an honest scope note on what that would additionally require.

## Firmware compatibility (official vs. Momentum)

`flipper-app/` is written against the stock Flipper API surface only (`furi`,
`gui`, `notification` - no OFW-only or Momentum-only calls), and Momentum
maintains compatibility with that surface. The same `.fap` should run
unmodified on both; CI builds against the official SDK channel via `ufbt`.

## Known limitations / where to look if something needs tuning

- The chat view's word-wrap and streaming-tail logic in
  `ai_chat_chat_view.c` are intentionally simple (fixed column width, no
  proportional font metrics) - tune `WRAP_COLS`/`VISIBLE_ROWS` if you change
  fonts or want denser text.
- One request in flight at a time, matching the protocol's lack of
  sequencing (see `docs/PROTOCOL.md`).
- `esp32-bridge` uses the generic `esp32-s2-saola-1` PlatformIO board
  definition, which matches the Devboard's UART/WiFi well enough but doesn't
  model devboard-specific peripherals (unused here anyway).
- SDK header drift: Flipper's SDK API shifts between firmware versions
  occasionally; if `ufbt build` complains about a renamed function, that's
  almost always a small rename in `furi_hal_serial.h` or the view API, not a
  structural issue.

## License

MIT - see `LICENSE`.

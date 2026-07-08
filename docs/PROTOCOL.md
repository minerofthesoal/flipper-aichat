# Wire protocol - Flipper <-> ESP32 Devboard

UART, 115200 baud, 8N1, newline (`\n`) terminated ASCII lines. Deliberately
not JSON on this leg: the Flipper has no JSON library in the base SDK and
limited RAM, so the format is a flat `TAG|payload` line. The ESP32 bridge
translates to/from JSON on its side when talking to LM Studio.

## Flipper -> ESP32

| Line                       | Meaning                                            |
|----------------------------|-----------------------------------------------------|
| `CHAT|<text>`              | Send a user chat message, triggers a streamed reply |
| `LISTMODELS`               | Ask for the list of models LM Studio has loaded      |
| `SELMODEL|<name>`          | Set the active model for subsequent `CHAT` calls     |
| `SETWIFI|<ssid>|<pass>`    | Provision WiFi creds directly (persisted in ESP32 NVS). Mostly superseded by the web installer's Improv flow - kept for headless/scripted setup from the Linux TUI. |
| `SETSERVER|<url>`          | Set the LM Studio base URL (persisted in ESP32 NVS)  |
| `REPROVISION`              | Drop WiFi so the device re-enters Improv provisioning mode without a physical reset - use this before reconnecting the web installer to an already-running board |

## ESP32 -> Flipper

| Line                  | Meaning                                                        |
|-----------------------|------------------------------------------------------------------|
| `TOK|<chunk>`         | One streamed token/delta of the assistant's reply. Zero or more per response, in order. |
| `DONE`                | End of the current response (or a completed/aborted request)   |
| `ERR|<message>`       | Something failed (bad model, HTTP error, no WiFi, etc.)         |
| `MODELS|<a>,<b>,<c>`  | Comma-separated list of model ids, sent in reply to `LISTMODELS` |
| `INFO|<message>`      | Status/log line (WiFi connecting, IP acquired, etc.)            |

Notes:
- `TOK` payloads have literal newlines escaped as `\n` (two characters) by
  the firmware, since a raw newline would terminate the line early.
- Payloads may contain `|` freely - only the *first* `|` on a line is treated
  as the tag delimiter.
- There's no message-id/sequencing; the protocol assumes one in-flight
  request at a time, matching the single-threaded UI on both ends.
- This text protocol only runs once the device has WiFi. Before that (fresh
  flash, or after `REPROVISION`), the serial port is owned entirely by the
  [Improv Wi-Fi](https://www.improv-wifi.com/) binary protocol instead, so
  the web installer can provision it. See `docs/SETUP.md` and the comments
  around `loop()` in `esp32-bridge/src/main.cpp`.

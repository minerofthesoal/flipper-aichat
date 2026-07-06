#pragma once

// Fallback defaults, used only the very first boot before any SETWIFI /
// SETSERVER command has been sent over UART. After that, values persist in
// NVS (Preferences) and these are ignored. It's fine to leave these blank
// and configure entirely from the Linux TUI or the Flipper app instead.

#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASS ""

// Point this at wherever `tailscale serve` (or `tailscale funnel`) is
// exposing LM Studio's OpenAI-compatible API, e.g.:
//   http://100.101.102.103:1234        (plain tailnet IP, no serve needed)
//   https://your-pc.your-tailnet.ts.net (via `tailscale serve https / 1234`)
#define DEFAULT_LM_STUDIO_URL ""

#define HTTP_TIMEOUT_MS 30000

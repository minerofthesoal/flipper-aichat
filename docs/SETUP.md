# Setup

## 1. LM Studio (on your home PC)

1. Load the model you want in LM Studio.
2. In LM Studio's "Local Server" tab, start the server (default port `1234`)
   and make sure "Serve on Local Network" is enabled so it's not bound only
   to `127.0.0.1`.

## 2. Tailscale (on your home PC)

You need LM Studio's API reachable from the ESP32 over plain HTTP. Two
options, easiest first:

- **Bare tailnet IP** - no extra config. Find your PC's tailnet IP with
  `tailscale ip -4`, and use `http://<that-ip>:1234` as the server URL. This
  works as long as the ESP32 device is also joined to the same tailnet
  reachable network... except the ESP32 *can't* run Tailscale. So this only
  works if your home WiFi and the devboard's WiFi are the same LAN as the
  PC. For anywhere-access, use `tailscale serve` below instead.
- **`tailscale serve`** - exposes the port over HTTPS to *other devices on
  your tailnet* without them needing to be on the same LAN:
  ```
  tailscale serve --bg --https=443 http://localhost:1234
  ```
  This gives you an address like `https://your-pc.your-tailnet.ts.net`.
  The devboard still needs some way onto that tailnet's reachable surface -
  since it can't run the Tailscale client, the practical setup is: run the
  devboard on the *same WiFi network* as a machine that can reach the
  `serve` URL, and just use the plain LAN IP instead; or use
  `tailscale funnel` (public HTTPS, no auth) if you're comfortable exposing
  the endpoint to the internet - not recommended for an unauthenticated LLM
  API without additionally putting a reverse proxy with auth in front of it.

The realistic setup for most people: **skip Tailscale for the ESP32 leg
entirely** and just point `SETSERVER` at your PC's plain LAN IP
(`http://192.168.1.x:1234`), since the devboard and your PC are both at
home anyway. Save Tailscale for when you want to reach the same LM Studio
instance from the Linux TUI while away from home (Direct mode already
supports a tailnet URL for exactly that).

## 3. Flash the ESP32 bridge firmware

### Option A: Web installer (recommended)

1. Go to `https://<your-github-username>.github.io/<repo-name>/` (published
   automatically by the `deploy-web-installer` CI job on every push to
   `main`). First time in this repo: enable it once under
   **Settings > Pages > Source: GitHub Actions**, then push to `main`.
2. Plug the WiFi Devboard into your computer over its own USB port (not
   through the Flipper), in Chrome or Edge on desktop (Web Serial isn't
   available elsewhere).
3. Click **Connect**, pick the devboard's serial port, and follow the
   install flow. It flashes the firmware, then walks you straight into
   WiFi setup in the browser via Improv Wi-Fi - no serial monitor needed.
4. Still on the page, enter your LM Studio address (see the Tailscale
   section above) and click **Connect & send** to set it over serial.

To reconfigure WiFi later on a board that's already flashed and running,
either come back to the page and use the button's "Configure Wi-Fi" option,
or send `REPROVISION` first (e.g. from the Linux TUI's Serial mode) so the
device drops back into provisioning mode.

### Option B: PlatformIO (for firmware development)

```
cd esp32-bridge
pio run -t upload -t monitor
```

First boot has no WiFi/server configured. Either use the web installer's
Improv flow (Option A, steps 3-4) for WiFi + the LM Studio address, or send
`SETWIFI|yourssid|yourpass` and `SETSERVER|http://192.168.1.x:1234` directly
over the PlatformIO serial monitor, or from the Linux TUI's Serial mode.

Config persists in NVS either way, so this is a one-time step per network.

## 4. Build and install the Flipper app

```
cd flipper-app
pip install --break-system-packages ufbt
ufbt launch     # builds and pushes to a connected Flipper over USB
```

Or grab the `.fap` from a tagged GitHub Release (built by CI) and copy it to
`SD Card/apps/Tools/` on the Flipper over qFlipper/USB mass storage.

Then attach the WiFi Devboard to the Flipper's GPIO header as normal, launch
"AI Chat" from Apps > Tools.

## 5. Linux TUI

```
cd linux-tui
pip install --break-system-packages .
ai-chat-tui
```

Edit `~/.config/ai-chat-tui/config.toml` (created on first run) to point
`lmstudio_url` at your PC (LAN IP at home, tailnet/`ts.net` URL away from
home) and `serial_port` at the devboard's `/dev/ttyACM*` or `/dev/ttyUSB*`
device if you'll use Serial mode.

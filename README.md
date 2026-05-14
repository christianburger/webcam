# web_cam — ESP32 Camera Firmware

ESP32-IDF firmware for the AI-Thinker ESP32-CAM. Exposes an HTTP server on
the local network; a Cloudflare Tunnel makes it reachable over HTTPS without
port-forwarding.

## Architecture

```
Browser → Cloudflare edge → cloudflared (LAN host) → ESP32 :80
                                                 ↕
                                   Spring Boot relay (runtracer.com)
                                   /api/** → ESP32 endpoints
```

The Spring Boot relay (`relay-backend/`) handles the public-facing SPA and
API; the ESP32 only needs to be reachable from the host running `cloudflared`.

## Firmware endpoints

| Path | Description |
|---|---|
| `GET /` | API-info JSON (HTML UI is served by Spring Boot) |
| `GET /capture` | Single JPEG frame |
| `GET /stream` | MJPEG stream (multipart/x-mixed-replace) |
| `GET /status` | `{heap, tasks, cpu}` JSON |
| `GET /hardware` | Chip info JSON |
| `GET /periph/state` | `{led, sw, pan, tilt}` JSON |
| `GET /control/pan?angle=N` | Set pan servo (0–180°) |
| `GET /control/tilt?angle=N` | Set tilt servo (0–180°) |
| `GET /control/led?state=N` | Flash LED on/off |
| `GET /control/switch?state=N` | Relay switch on/off |

## Build and flash

```bash
# Configure Wi-Fi credentials
idf.py menuconfig        # WiFi Configuration → SSID / Password

idf.py build
idf.py flash monitor
```

After boot the serial log prints the assigned LAN IP and confirms
`http://web-cam.local/` is reachable via mDNS.

## Cloudflare Tunnel setup

### One-time setup

```bash
cloudflared tunnel login                      # authorises runtracer.com
cloudflared tunnel create esp32-webcam        # save the printed UUID
cloudflared tunnel route dns esp32-webcam cam.runtracer.com
```

### Config file (`~/.cloudflared/config.yml`)

```yaml
tunnel: <TUNNEL_UUID>
credentials-file: /home/chris/.cloudflared/<TUNNEL_UUID>.json

ingress:
  - hostname: cam.runtracer.com
    service: http://web-cam.local:80
  - service: http_status:404
```

> `cloudflared` ingress does not support path suffixes in `service:` URLs.
> Keep `service` at the origin root and use request paths in the browser URL.

### Run as a service

```bash
sudo cloudflared service install
sudo systemctl enable --now cloudflared
```

Verify: `https://cam.runtracer.com/status` should return JSON.

### mDNS auto-watcher

If the ESP32 LAN IP changes, `scripts/cloudflared-mdns-watch.sh` resolves
`web-cam.local`, updates `config.yml`, and restarts `cloudflared` automatically.
Only works with **locally managed** tunnels (config-file mode, not `--token`
mode).

```bash
export TUNNEL_UUID="<your-uuid>"
export MDNS_NAME="web-cam.local"
./scripts/cloudflared-mdns-watch.sh
```

Optional env vars: `CHECK_INTERVAL_SEC` (default 15), `CONFIG_PATH`,
`CLOUDFLARED_SERVICE`.

## Local mock origin (test without ESP32)

```bash
sudo PORT=80 go run ./tools/mock_origin/main.go
```

Serves `/`, `/status`, `/capture`, `/stream`, `/hardware` on port 80.
Point the tunnel config at `http://127.0.0.1:80` to validate the tunnel
end-to-end without hardware.

## Troubleshooting

### 524 Timeout

Cloudflare reached the tunnel but the origin didn't respond in time.

```bash
# Test origin directly from the cloudflared host
curl -v --max-time 5 http://web-cam.local/status

# Check tunnel mode (token = remotely managed, ignores local config.yml)
systemctl cat cloudflared | grep -E -- '--token|--config'

# Tail cloudflared logs
journalctl -u cloudflared -n 200 --no-pager
```

### 431 Request Header Fields Too Large

Almost always stale/large cookies for `runtracer.com`.

```bash
# Confirm curl works (minimal headers)
curl -sS -o /dev/null -w "%{http_code}\n" https://cam.runtracer.com/

# If curl works but browser fails → clear site data for cam.runtracer.com
# or test in a private window
```

### Correlating requests with ESP32 serial logs

The handlers for `/`, `/status`, `/capture`, and `/stream` log the
`CF-Ray` header to serial. Match it against the browser's response headers
to confirm a request reached the ESP32:

```bash
idf.py monitor              # ESP32 serial
journalctl -u cloudflared -f   # cloudflared on the LAN host
```

If cloudflared logs show attempts but the ESP32 prints nothing, traffic is
not reaching the HTTP server.

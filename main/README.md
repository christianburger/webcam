# Web Cam - ESP32 Camera Web Server (Cloudflare Tunnel Edition)

This project now exposes the ESP32 web interface directly through a **Cloudflare Tunnel**.
There is no Cloudflare Worker relay, no KV, and no browser-side frame decryption path.

## Architecture

- ESP32 runs the HTTP UI locally (root UI, `/stream`, `/capture`, `/status`, `/hardware`).
- `cloudflared` runs on a host in the same LAN as the ESP32 and creates an outbound tunnel to Cloudflare.
- Your domain (`runtracer.com`) maps a hostname (for example `cam.runtracer.com`) to that tunnel.
- Browser connects to Cloudflare edge over HTTPS and Cloudflare forwards to `http://<esp32-lan-ip>:80`.

## 1) Firmware setup (ESP32)

1. Configure Wi-Fi credentials in `main/network_manager.h`.
2. Build and flash:
   ```bash
   idf.py build
   idf.py flash monitor
   ```
3. After boot, note the ESP32 LAN IP in serial logs (`IP: ...`).

You should be able to open locally:

- `http://web-cam.local/` (mDNS)
- or `http://<ESP32_LAN_IP>/`

## 2) Cloudflare Zero Trust setup (cloud infrastructure)

1. Log in to Cloudflare dashboard for `runtracer.com`.
2. Open **Zero Trust** (one-time onboarding if needed).
3. Go to **Networks > Tunnels**.
4. Click **Create a tunnel**.
5. Choose **Cloudflared**.
6. Name it, e.g. `esp32-webcam`.
7. Cloudflare will show an install/connect command token for Linux.

Do **not** create Worker routes for this app. The tunnel hostname replaces Worker access.

## 3) Local `cloudflared` setup on Gentoo host

You already have `net-vpn/cloudflared` installed.

### A. Authenticate cloudflared

```bash
cloudflared tunnel login
```

This opens a browser; select `runtracer.com`. Cloudflare writes a cert to `~/.cloudflared/`.

### B. Create named tunnel

```bash
cloudflared tunnel create esp32-webcam
```

Save the generated tunnel UUID.

### C. Create config file

Create `~/.cloudflared/config.yml` (single-hostname setup used by the UI by default):

```yaml
tunnel: <TUNNEL_UUID>
credentials-file: /home/chris/.cloudflared/<TUNNEL_UUID>.json

ingress:
  - hostname: cam.runtracer.com
    service: http://<ESP32_LAN_IP>:80
  - service: http_status:404
```

Replace:

- `<TUNNEL_UUID>` with the tunnel UUID
- `<ESP32_LAN_IP>` with the ESP32 IP from serial logs

If you want dedicated hostnames per endpoint (like your current setup), this is also valid:

```yaml
tunnel: <TUNNEL_UUID>
credentials-file: /home/chris/.cloudflared/<TUNNEL_UUID>.json

ingress:
  - hostname: cam.runtracer.com
    service: http://<ESP32_LAN_IP>:80
  - hostname: cam-status.runtracer.com
    service: http://<ESP32_LAN_IP>:80
  - hostname: cam-stream.runtracer.com
    service: http://<ESP32_LAN_IP>:80
  - service: http_status:404
```

> Important: Cloudflared ingress does **not** support putting `/status` or `/stream` in `service:` URLs. Keep `service` at origin root (`http://<ESP32_LAN_IP>:80`), and use request paths (`/status`, `/stream`) in the browser URL.
>
> Note: the embedded web UI requests `/stream` and `/status` using relative paths on the same origin, so `cam.runtracer.com` must keep routing to `http://<ESP32_LAN_IP>:80`. That means `https://cam.runtracer.com/status` should always be valid when `cam.runtracer.com` is routed correctly.

### D. Create DNS route in Cloudflare

```bash
cloudflared tunnel route dns esp32-webcam cam.runtracer.com
```

This creates/updates a proxied CNAME in Cloudflare DNS.

### E. Run tunnel

```bash
cloudflared tunnel run esp32-webcam
```

Then open:

- `https://cam.runtracer.com/`
- stream: `https://cam.runtracer.com/stream`
- capture: `https://cam.runtracer.com/capture`
- optional dedicated stream hostname: `https://cam-stream.runtracer.com/`
- optional dedicated status hostname: `https://cam-status.runtracer.com/`
- when using dedicated hostnames, append endpoint path explicitly (for example `https://cam-status.runtracer.com/status`).

## 4) Run tunnel as a service (recommended)

On systems using systemd (including many Gentoo installs):

```bash
sudo cloudflared service install
sudo systemctl enable --now cloudflared
systemctl status cloudflared
```

If your Gentoo profile uses OpenRC instead, run `cloudflared` with your own supervised service wrapper and the same `config.yml`.

## 5) Remove old Worker relay assets

The relay path was removed from this repo:

- no poller task
- no Worker backend dependency
- no `wrangler.toml`

If you previously deployed a Worker for this project, remove old DNS/routes referencing it to avoid confusion.

## Endpoints served by ESP32

- `GET /` main UI
- `GET /capture` single JPEG
- `GET /stream` MJPEG stream
- `GET /status` runtime telemetry JSON
- `GET /hardware` hardware capability JSON

## Troubleshooting

1. `https://cam.runtracer.com` loads but no video:
   - test local first: `http://<ESP32_LAN_IP>/stream`
   - check `cloudflared` logs for upstream errors.
2. Tunnel connected but hostname fails:
   - verify route exists: `cloudflared tunnel route dns ...`
   - confirm proxied DNS record in Cloudflare.
3. Intermittent disconnects:
   - pin ESP32 IP in router DHCP reservation.
   - reduce Wi-Fi contention / improve signal.

## Dynamic LAN IP options

Because the ESP32 LAN IP is dynamic, prefer one of these approaches:

1. Use a local DNS name that resolves on your LAN (for example `web-cam.local`) and point Cloudflared at it if your host resolves mDNS reliably.
2. Regenerate `config.yml` from a script and restart Cloudflared when mDNS resolves to a new IP.

You can use an environment variable for the local IP, but only if you expand it before Cloudflared reads `config.yml` (for example with a wrapper script that templates the file). Keep in mind that when IP changes, the tunnel process must reload/restart to pick up the new upstream value.

### mDNS watcher script (auto-rewrite + restart)

This repo now includes `scripts/cloudflared-mdns-watch.sh`.
It is for **locally managed tunnels** (where Cloudflared reads ingress from `config.yml`).
If your systemd unit runs `cloudflared ... --token ...`, that is a **remotely managed tunnel**, and local `config.yml` edits will not update dashboard ingress.

It continuously:

- resolves `web-cam.local` (or your `MDNS_NAME`)
- checks `http://<resolved-ip>:80/status`
- updates IP in `~/.cloudflared/config.yml` using `sed` if IP changed or healthcheck fails
- restarts Cloudflared so the tunnel re-establishes with the new upstream IP

Run it like this:

```bash
export TUNNEL_UUID="e94ed88f-c9e9-4571-bdf9-3cf4e60f49a7"
export MDNS_NAME="web-cam.local"
export HOSTNAME_MAIN="cam.runtracer.com"
# Optional (leave unset to only use HOSTNAME_MAIN):
export HOSTNAME_STATUS="cam-status.runtracer.com"
export HOSTNAME_STREAM="cam-stream.runtracer.com"
./scripts/cloudflared-mdns-watch.sh
```

Optional variables:

- `CHECK_INTERVAL_SEC` (default `15`)
- `CONFIG_PATH` (default `~/.cloudflared/config.yml`)
- `CLOUDFLARED_SERVICE` (default `cloudflared`)

## Error 524 troubleshooting (Cloudflare works, Host errors)

If Cloudflare dashboard shows published apps but browser gets **524 timeout**, test in this order:

1. From the machine running Cloudflared, test origin directly:
   - `curl -v --max-time 5 http://192.168.0.175:80/status`
   - `curl -I --max-time 5 http://192.168.0.175:80/`
2. Check Cloudflared mode:
   - `systemctl cat cloudflared | grep -E -- '--token|--config'`
   - If you see `--token`, local `config.yml`/watcher changes are ignored.
3. Check Cloudflared logs:
   - `journalctl -u cloudflared -n 200 --no-pager`
4. Verify LAN reachability/firewall from Cloudflared host to ESP32:
   - `ping 192.168.0.175`
   - ensure no host firewall blocks outbound to `192.168.0.175:80`

If step (1) fails, fix local network/origin first; Cloudflare cannot proxy an unreachable local origin.

## Error 431 troubleshooting (`Request Header Fields Too Large`)

If browser shows:

- `GET https://cam.runtracer.com/ 431 (Request Header Fields Too Large)`
- `GET /favicon.ico 431`

then the request reached Cloudflare, but request headers are too large before origin handling.

Most common causes:

1. Very large/stale cookies for `runtracer.com` (often from repeated auth redirects or old app cookies).
2. Cloudflare Access / WAF policy loops adding large auth headers/cookies.
3. Browser extensions injecting large headers.

Quick fixes:

1. Clear cookies/site data for `cam.runtracer.com` (or all `runtracer.com` data), then hard refresh.
2. Test in private/incognito window (no extensions, clean cookie jar).
3. Test with curl (minimal headers) using `GET` (not `HEAD`):
   - `curl -sS -o /dev/null -w "%{http_code}\n" https://cam.runtracer.com/`
   - `curl -sS -o /dev/null -w "%{http_code}\n" https://cam.runtracer.com/status`
4. In Cloudflare dashboard, verify no Access policy is forcing repeated redirects/tokens for this hostname.

Note: `curl -I` sends `HEAD`; this firmware only registers `GET` handlers, so `405` for `HEAD` is expected and does not prove tunnel failure.

If curl `GET` works but browser fails with 431, it is almost always browser cookie/header bloat on that hostname.

## Local mock origin (test tunnel without ESP32)

If you want to test Cloudflare Tunnel while the ESP32 is powered off, run:

```bash
go run ./tools/mock_origin/main.go
```

This serves compatible test endpoints:

- `/`
- `/status`
- `/capture`
- `/stream` (MJPEG multipart)
- `/hardware`

Default listen port is `8080`. To match your tunnel origin on port `80`:

```bash
sudo PORT=80 go run ./tools/mock_origin/main.go
```

Then point the tunnel origin service to `http://127.0.0.1:8080` (or `:80`) and validate from browser/curl.

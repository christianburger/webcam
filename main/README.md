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

Create `~/.cloudflared/config.yml`:

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

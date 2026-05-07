# Cloudflare Tunnel Setup & Ops Guide (ESP32 Webcam)

> Note: filename kept as `cloudfare.md` per project request.

## 1) Architecture

- Browser -> Cloudflare Edge (HTTPS)
- Cloudflare Tunnel connector (`cloudflared`) on LAN host
- Origin service on LAN (`http://<ESP32_IP>:80`)

## 2) Two tunnel modes

### A. Remotely-managed (token mode)
- Process starts with `cloudflared tunnel run --token ...`
- Ingress/origin mapping is managed from Cloudflare dashboard
- Local `~/.cloudflared/config.yml` edits do **not** control routing

### B. Locally-managed (config.yml mode)
- Process reads local `~/.cloudflared/config.yml`
- Local automation scripts can update origin IP

## 3) Minimal ingress rule (important)

For HTTP origin, keep service at origin root:

```yaml
ingress:
  - hostname: cam.runtracer.com
    service: http://192.168.0.175:80
  - service: http_status:404
```

Do **not** use `/status` or `/stream` suffix in `service:` URLs.

## 4) Verify health

```bash
# Local origin from tunnel host
curl -v --max-time 5 http://192.168.0.175:80/status

# Public edge path
curl -sS -o /dev/null -w "%{http_code}\n" https://cam.runtracer.com/
curl -sS -o /dev/null -w "%{http_code}\n" https://cam.runtracer.com/status
```

## 5) Authentication (Cloudflare Access, free tier-friendly)

- Zero Trust -> Access -> Applications -> Add application -> Self-hosted
- Hostname: `cam.runtracer.com`
- Policy: `Allow` only specific emails (e.g., 4 users)
- Deny-by-default covers everyone else

### OTP note
When using email OTP, Cloudflare may show a generic "code sent" response even when delivery fails (privacy behavior). If codes are not arriving, test with Gmail and check mailbox filtering/suppression.

## 6) 431 "Header fields are too long" after Access

Access cookies can increase header size. Firmware parser limits should be raised in `sdkconfig`:

- `CONFIG_HTTPD_MAX_REQ_HDR_LEN=4096`
- `CONFIG_HTTPD_MAX_URI_LEN=2048`

These are compile-time limits in ESP-IDF for this project/version.

## 7) Serial/journal correlation

ESP32 serial:
```bash
idf.py monitor
```

Cloudflared logs:
```bash
journalctl -u cloudflared -f
```

Correlate `CF-Ray` header in edge response with ESP32 request logs to confirm Cloudflare-routed requests reach firmware handlers.

## 8) Pause/resume tunnel quickly

```bash
sudo systemctl stop cloudflared
sudo systemctl start cloudflared
```

## 9) Dynamic IP strategy

- Best: stable DHCP reservation (if available)
- If not available: use mDNS watcher script for locally-managed tunnels
- For token mode: update origin in Cloudflare dashboard/API (local config automation won't apply)

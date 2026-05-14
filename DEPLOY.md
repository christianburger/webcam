# Deployment Guide

## Architecture

```
Browser
  │  HTTPS
  ▼
Cloudflare edge  (cam.runtracer.com)
  │  Cloudflare Tunnel
  ▼
cloudflared  (Gentoo host, systemd)
  │  HTTP  localhost:8080
  ▼
relay-backend  :8080  (Docker container or bare JVM)
  ├── serves Angular SPA  at  /
  └── proxies ESP32 API   at  /api/**
        │  HTTP (LAN)
        ▼
      ESP32-CAM  (web-cam.local / 192.168.x.x)
```

All public traffic goes through **one** Cloudflare hostname (`cam.runtracer.com`)
pointing to `http://localhost:8080`. The ESP32 never needs its own hostname.

---

## 1  Build and run with Docker

### Prerequisites

| Tool | Notes |
|------|-------|
| Docker ≥ 24 | `docker version` |
| avahi-daemon | Required for `web-cam.local` mDNS resolution; must be running and its socket mounted into the container |

```bash
# Enable avahi on boot and start it now
sudo systemctl enable --now avahi-daemon

# Confirm the socket exists before running the container
ls /run/avahi-daemon/socket
```

### Build the image

Run from the **repository root** (`webcam/`). The `Dockerfile` lives inside
`relay-backend/` and the build context must include both `relay-backend/` and
`frontend/`:

```bash
# From webcam/
docker build -t relay-backend -f relay-backend/Dockerfile .
```

First run takes 3–5 minutes (Maven and npm dependency downloads are cached in
subsequent builds).

### Run — mDNS (standard)

`--network host` shares the host network stack. The avahi socket is mounted in
so the container's `libnss-mdns` can reach the host's avahi daemon to resolve
`.local` names:

```bash
docker run -d \
  --name relay-backend \
  --network host \
  -v /run/avahi-daemon/socket:/run/avahi-daemon/socket \
  relay-backend
```

### Run — static IP (if mDNS is unreliable)

Override the camera host via Spring Boot's relaxed-binding env vars. The
property `app.camera.registry[0].host` maps to the env var below:

```bash
docker run -d \
  --name relay-backend \
  -p 8080:8080 \
  -e APP_CAMERA_REGISTRY_0_HOST=192.168.0.175 \
  relay-backend
```

For multiple overrides at once, `SPRING_APPLICATION_JSON` is easier to read:

```bash
docker run -d \
  --name relay-backend \
  -p 8080:8080 \
  -e SPRING_APPLICATION_JSON='{"app":{"camera":{"registry":[{"id":"cam-1","name":"ESP32-CAM Front Door","host":"192.168.0.175","port":80}]}}}' \
  relay-backend
```

### Verify

```bash
docker logs -f relay-backend           # watch startup, confirm "Tomcat started on port 8080"
curl http://localhost:8080/api/health  # → {"status":"ok","observedAt":"..."}
curl http://localhost:8080/            # → 200 (Angular index.html)
```

---

## 2  Auto-start on boot (systemd)

Let systemd own the container lifecycle. **Do not** pass `--restart` to
`docker run` — that creates a conflict where both Docker and systemd try to
restart the process independently.

Create `/etc/systemd/system/relay-backend.service`:

```ini
[Unit]
Description=ESP32-CAM relay backend (Docker)
After=network-online.target docker.service avahi-daemon.service
Requires=docker.service avahi-daemon.service
Wants=network-online.target

[Service]
Restart=always
RestartSec=5s
# Remove any leftover container from a previous run before starting
ExecStartPre=-/usr/bin/docker rm -f relay-backend
ExecStart=/usr/bin/docker run \
  --name relay-backend \
  --network host \
  -v /run/avahi-daemon/socket:/run/avahi-daemon/socket \
  relay-backend
ExecStop=/usr/bin/docker stop relay-backend

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now relay-backend
sudo systemctl status relay-backend
```

To pass env vars (e.g. static IP), add to `[Service]`:

```ini
Environment=APP_CAMERA_REGISTRY_0_HOST=192.168.0.175
```

---

## 3  Reconfigure the Cloudflare Tunnel

The running cloudflared service uses a **remote-managed token** (`--token …`).
Ingress rules live in the Cloudflare dashboard — local `config.yml` is ignored.

### Target configuration

`cam.runtracer.com` → `http://localhost:8080`

Spring Boot serves the Angular SPA at `/` and proxies the ESP32 API at `/api/**`.

### Steps

1. Open **Cloudflare Zero Trust** → <https://one.dash.cloudflare.com/>
2. **Networks → Tunnels**
3. Find tunnel `esp32-webcam` (UUID `e94ed88f-c9e9-4571-bdf9-3cf4e60f49a7`) → **Configure**
4. **Public Hostnames** tab
5. Edit `cam.runtracer.com`:
   - **Service type:** `HTTP`
   - **URL:** `localhost:8080`
   - Leave TLS and access-policy settings as-is
6. **Save hostname**
7. Delete stale hostnames (`cam-status.runtracer.com`, `cam-stream.runtracer.com`)
   if they still exist.

Cloudflare pushes the new ingress config to `cloudflared` within seconds —
no restart of the local cloudflared service is needed.

### Verify end-to-end

```bash
# From anywhere on the internet
curl -sS https://cam.runtracer.com/api/health
# → {"status":"ok","observedAt":"2026-..."}
```

502 / 524 diagnostic steps:

```bash
docker ps | grep relay-backend              # is the container running?
systemctl status cloudflared                # is the tunnel running?
curl -v http://localhost:8080/api/health    # can cloudflared reach the backend?
journalctl -u cloudflared -n 100 --no-pager
```

---

## 4  Updating the image

```bash
# From webcam/
docker build -t relay-backend -f relay-backend/Dockerfile .
sudo systemctl restart relay-backend       # if using the systemd unit above
```

Manual restart without systemd:

```bash
docker build -t relay-backend -f relay-backend/Dockerfile .
docker stop relay-backend && docker rm relay-backend
docker run -d \
  --name relay-backend \
  --network host \
  -v /run/avahi-daemon/socket:/run/avahi-daemon/socket \
  relay-backend
```

---

## 5  Environment variable reference

Spring Boot maps environment variables to properties using relaxed binding:
`UPPER_SNAKE_CASE` → `lower.kebab.case`, `_0_` → `[0]`.

| Environment variable | Property | Default | Purpose |
|---|---|---|---|
| `APP_CAMERA_REGISTRY_0_HOST` | `app.camera.registry[0].host` | `web-cam.local` | mDNS name or LAN IP of the ESP32 |
| `APP_CAMERA_REGISTRY_0_PORT` | `app.camera.registry[0].port` | `80` | HTTP port of the ESP32 |
| `APP_CAMERA_DEFAULT_TIMEOUT_MS` | `app.camera.default-timeout-ms` | `5000` | Relay request timeout |
| `SERVER_PORT` | `server.port` | `8080` | Port the Spring Boot app listens on |

Pass with `-e VAR=value` in `docker run`, or as `Environment=VAR=value` lines
in the systemd `[Service]` section.

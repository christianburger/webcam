# Infrastructure Reference

Documents the current state of the ESP32-CAM relay infrastructure.
For day-to-day deployment operations see [DEPLOY.md](DEPLOY.md).

---

## Host

| Item | Value |
|------|-------|
| OS | Gentoo Linux x86_64 |
| Kernel | 7.0.5-burger |
| CPU | AMD Ryzen 5 1600 |
| RAM | 32 GB |
| Shell | bash 5.3.9 |
| DE/WM | Plasma 6.6.4 / kwin |

---

## Repository layout

```
webcam/
├── main/                        # ESP32-IDF firmware (C)
├── frontend/
│   └── relay-frontend/          # Angular 21 SPA
├── relay-backend/               # Spring Boot 3.5 gateway (Java 21)
│   ├── Dockerfile               # Multi-stage; build context = webcam/
│   └── src/main/resources/
│       ├── application.properties
│       └── static/              # Angular production build output
├── partitions.csv
├── CMakeLists.txt
└── sdkconfig
```

---

## Runtime architecture

```
Browser
  │  HTTPS
  ▼
Cloudflare edge  (cam.runtracer.com)
  │  Cloudflare Tunnel  (token-managed, no local config.yml)
  ▼
cloudflared  (Gentoo host, systemd unit: cloudflared)
  │  HTTP  localhost:8080
  ▼
relay-backend  (Docker container or bare JVM, systemd unit: relay-backend)
  ├── GET /          → Angular SPA (static files)
  └── GET /api/**    → proxied to ESP32 over LAN
        │  HTTP
        ▼
      ESP32-CAM  (web-cam.local / 192.168.0.x, port 80)
```

One public hostname. One Cloudflare tunnel. The ESP32 is LAN-only.

---

## Host dependencies

Required packages on the Gentoo host:

```bash
sudo emerge --sync
sudo emerge -av \
  dev-java/openjdk:21 \
  dev-java/maven-bin \
  net-libs/nodejs \
  app-containers/docker \
  app-containers/docker-cli \
  net-dns/avahi
```

Verify:

```bash
java -version    # 21.x
mvn -v           # 3.9.x
node -v          # 22.x
npm -v           # 11.x
docker version   # 24+
```

Docker group membership (re-login after):

```bash
sudo usermod -aG docker "$USER"
```

Enable services on boot:

```bash
sudo systemctl enable --now docker
sudo systemctl enable --now avahi-daemon
```

---

## Cloudflare Tunnel

| Item | Value |
|------|-------|
| Tunnel name | `esp32-webcam` |
| Tunnel UUID | `e94ed88f-c9e9-4571-bdf9-3cf4e60f49a7` |
| Management mode | Remote (token), configured via Cloudflare dashboard |
| Public hostname | `cam.runtracer.com` |
| Tunnel origin | `http://localhost:8080` |
| systemd unit | `cloudflared` |

Because the tunnel runs in token mode, local `config.yml` edits have no
effect. All ingress changes are made in **Cloudflare Zero Trust → Networks →
Tunnels → esp32-webcam → Configure → Public Hostnames**.

---

## Camera registry

The camera list is configured in
`relay-backend/src/main/resources/application.properties`:

```properties
app.camera.registry[0].id=cam-1
app.camera.registry[0].name=ESP32-CAM Front Door
app.camera.registry[0].host=web-cam.local
app.camera.registry[0].port=80
```

To add a second camera, append `registry[1].*` entries. Override at runtime
with env vars (see `DEPLOY.md § Environment variable reference`).

---

## Runtime request flow

1. Browser → `https://cam.runtracer.com`
2. Cloudflare Access authenticates the user (if an Access policy is set)
3. Cloudflare Tunnel → `cloudflared` → `relay-backend:8080`
4. Spring Boot serves `index.html`; Angular bootstraps in the browser
5. SPA calls `GET /api/cameras/cam-1/frame/latest`
6. Spring Boot resolves `web-cam.local`, calls `GET http://web-cam.local/capture`
7. Spring Base64-encodes the JPEG and returns JSON to the SPA
8. SPA renders the frame

---

## Validation

```bash
# Backend health (local)
curl http://localhost:8080/api/health

# Backend health (through Cloudflare)
curl https://cam.runtracer.com/api/health

# Camera status relay
curl https://cam.runtracer.com/api/cameras/cam-1/status

# ESP32 direct (LAN only)
curl http://web-cam.local/status
```

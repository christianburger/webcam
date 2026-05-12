# ESP32-CAM Relay Infrastructure Setup (Angular + Spring Boot)

This guide implements the architecture you described:

1. User opens a **single public URL** behind Cloudflare Access.
2. User authenticates with Cloudflare.
3. Browser loads Angular SPA.
4. SPA calls backend API endpoints.
5. Backend resolves camera mDNS/LAN IP, calls ESP32 endpoints, and relays data back to SPA.

- **Single public URL:** `https://runtracer.com`
- **Cloudflare Tunnel public hostname:** `runtracer.com`
- **Backend role:** API + camera relay + optional static SPA hosting
- **Frontend role:** UI only

---

## 1) Host profile (provided)

- OS: Gentoo Linux x86_64
- Kernel: 7.0.5-burger
- Shell: bash 5.3.9
- DE/WM: Plasma 6.6.4 / kwin
- CPU: AMD Ryzen 5 1600
- GPU: NVIDIA GTX 1650
- RAM: 32 GB

---

## 2) Install required infrastructure on Gentoo

```bash
sudo emerge --sync
sudo emerge -avuDN @world
sudo emerge -av dev-java/openjdk:21 dev-java/maven-bin net-libs/nodejs
sudo npm install -g @angular/cli
```

Verify:

```bash
java -version
mvn -v
node -v
npm -v
ng version
```

Optional Docker:

```bash
sudo emerge -av app-containers/docker app-containers/docker-cli
sudo rc-update add docker default
sudo rc-service docker start
sudo usermod -aG docker "$USER"
```

---

## 3) Create folders

```bash
mkdir -p backend frontend
```

---

## 4) Bootstrap backend (Spring Boot) — exact Initializr choices

Use **Spring Initializr** with the following exact values:

- Project: **Maven**
- Language: **Java**
- Spring Boot: **3.5.x (latest stable shown by Initializr)**
- Group: **com.runtracer**
- Artifact: **relay-backend**
- Name: **relay-backend**
- Package name: **com.runtracer.relaybackend**
- Packaging: **Jar**
- Java: **21**
- Dependencies:
  - **Spring Web**
  - **Spring WebSocket**
  - **Spring Boot Actuator**
  - **Validation**
  - **Spring Security**

### 4.1 One-command download from Initializr (recommended)

From repo root:

```bash
mkdir -p backend
curl -fsSL "https://start.spring.io/starter.zip?type=maven-project&language=java&bootVersion=3.5.0&baseDir=relay-backend&groupId=com.runtracer&artifactId=relay-backend&name=relay-backend&description=ESP32-CAM%20relay%20backend&packageName=com.runtracer.relaybackend&packaging=jar&javaVersion=21&dependencies=web,websocket,actuator,validation,security" -o /tmp/relay-backend.zip
unzip -q /tmp/relay-backend.zip -d backend
```

This creates:

```text
backend/relay-backend
```

### 4.2 Set runtracer URI in backend config

Edit `backend/relay-backend/src/main/resources/application.yml`:

```yaml
server:
  port: 8080

app:
  public-base-url: https://runtracer.com
  camera:
    resolve-ttl-seconds: 60
```

### 4.3 API contract (backend-owned endpoints)
- `GET /api/cameras`
- `POST /api/cameras/{id}/session/start`
- `POST /api/cameras/{id}/session/stop`
- `GET /api/cameras/{id}/status`
- `GET /api/cameras/{id}/frame/latest`
- `GET /api/health`

### 4.4 Backend responsibilities
- Resolve camera hostnames (mDNS, e.g. `cam-a.local`).
- Cache resolved IP with TTL and re-resolve on failure.
- Call ESP32 endpoints (`/status`, `/stream`, `/capture`) on LAN.
- Normalize and relay payloads to SPA.

---

## 5) Bootstrap frontend (Angular SPA)

```bash
cd frontend
npx @angular/cli@latest new relay-frontend --routing --style=scss
```

Use API URL in Angular:

`src/environments/environment.ts`
```ts
export const environment = {
  production: false,
  apiBaseUrl: '/api'
};
```

Using relative `/api` keeps one public origin (`https://runtracer.com`) for both SPA and backend endpoints.

---

## 6) Single-URL deployment model (recommended)

To match your requirement (one Cloudflare URL for SPA + API), use this topology:

- Cloudflare Tunnel hostname: `runtracer.com`
- Tunnel origin service: `http://127.0.0.1:8080`
- Spring Boot serves:
  - Static SPA files at `/`
  - API at `/api/**`

That means Cloudflare only points to one origin (Spring Boot), and Spring Boot is your gateway.

### Why this fits your flow
- User always hits `https://runtracer.com`.
- Cloudflare Access authenticates once for that app.
- SPA and API share origin/session/cookies.
- Backend owns all LAN camera access and mDNS resolution.

---

## 7) Cloudflare Tunnel config for single URL

`~/.cloudflared/config.yml` (locally managed tunnel):

```yaml
tunnel: <TUNNEL_UUID>
credentials-file: /home/<user>/.cloudflared/<TUNNEL_UUID>.json

ingress:
  - hostname: runtracer.com
    service: http://127.0.0.1:8080
  - service: http_status:404
```

Important:
- `service` remains root host:port (no `/api` path in config).
- Path routing happens in HTTP request (`/`, `/api/...`) and Spring handles it.

If tunnel runs in token mode (`--token`), configure ingress in Cloudflare dashboard.

---

## 8) Runtime flow (end-to-end)

1. Browser -> `https://runtracer.com`
2. Cloudflare Access auth
3. Cloudflare Tunnel -> Spring Boot (127.0.0.1:8080)
4. Spring serves SPA
5. SPA calls `/api/cameras/...`
6. Spring resolves mDNS and calls ESP32 on LAN
7. Spring returns payload to SPA

This is exactly: endpoints live on backend; Cloudflare exposes one URL.

---

## 9) Validation commands

```bash
curl -sS -o /dev/null -w "%{http_code}\n" https://runtracer.com/
curl -sS -o /dev/null -w "%{http_code}\n" https://runtracer.com/api/health
```

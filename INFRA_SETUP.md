# ESP32-CAM Relay Infrastructure Setup (Angular + Spring Boot)

This guide replaces direct use of the ESP32-CAM web interface with a custom stack:

- **Backend:** Java Spring Boot API/gateway
- **Frontend:** Angular SPA
- **Device:** ESP32-CAM on local network
- **Public/API base URL:** `https://runtracer.com`

---

## 1) Host profile (detected / provided)

Use these instructions for:

- OS: **Gentoo Linux x86_64**
- Kernel: **7.0.5-burger**
- Shell: **bash 5.3.9**
- DE/WM: Plasma 6.6.4 / kwin
- CPU: AMD Ryzen 5 1600
- GPU: NVIDIA GTX 1650
- RAM: 32 GB

---

## 2) Install required infrastructure on Gentoo

### 2.1 Sync portage and update world
```bash
sudo emerge --sync
sudo emerge -avuDN @world
```

### 2.2 Install Java 21 (OpenJDK)
```bash
sudo emerge -av dev-java/openjdk:21
```

Set Java 21 as default:
```bash
sudo eselect java-vm list
sudo eselect java-vm set system <INDEX_OF_OPENJDK_21>
java -version
```

### 2.3 Install Maven
```bash
sudo emerge -av dev-java/maven
mvn -v
```

### 2.4 Install Node.js LTS + npm
```bash
sudo emerge -av net-libs/nodejs
node -v
npm -v
```

### 2.5 Install Angular CLI
```bash
sudo npm install -g @angular/cli
ng version
```

### 2.6 Optional (recommended): Docker + Compose
```bash
sudo emerge -av app-containers/docker app-containers/docker-cli
sudo rc-update add docker default
sudo rc-service docker start
sudo usermod -aG docker "$USER"
```
Log out/in once after adding your user to `docker` group.

---

## 3) Create backend/frontend folders

From repository root:

```bash
mkdir -p backend frontend
```

---

## 4) Bootstrap backend (Spring Boot)

### 4.1 Generate Spring Boot project
Use Spring Initializr with:
- Group: `com.runtracer`
- Artifact: `relay-backend`
- Name: `relay-backend`
- Java: 21
- Dependencies:
  - Spring Web
  - Spring WebSocket
  - Spring Boot Actuator
  - Spring Security (optional)

Put generated project under:

```text
backend/relay-backend
```

### 4.2 Backend base URL config
Set your backend public URL to:

```text
https://runtracer.com
```

For local dev, keep localhost profile and production profile separate.

Example `application.yml` idea:
- dev API root: `http://localhost:8080`
- prod API root: `https://runtracer.com`

### 4.3 Suggested first API endpoints
- `POST /api/camera/session/start`
- `POST /api/camera/session/stop`
- `GET /api/camera/frame/latest`
- `GET /api/health`

### 4.4 Run backend
```bash
cd backend/relay-backend
./mvnw spring-boot:run
```

---

## 5) Bootstrap frontend (Angular SPA)

```bash
cd frontend
npx @angular/cli@latest new relay-frontend --routing --style=scss
```

When prompted:
- Enable SSR: **No**
- Zone.js: **Yes**

### 5.1 Configure environment URLs
In `frontend/relay-frontend/src/environments/environment.ts` (local):
```ts
export const environment = {
  production: false,
  apiBaseUrl: 'http://localhost:8080/api'
};
```

In `environment.prod.ts` (production):
```ts
export const environment = {
  production: true,
  apiBaseUrl: 'https://runtracer.com/api'
};
```

### 5.2 Run frontend
```bash
cd frontend/relay-frontend
npm install
npm start
```

---

## 6) Local + production workflow

### Local development
- Backend: `http://localhost:8080`
- Frontend: `http://localhost:4200`
- CORS allow origin: `http://localhost:4200`

### Production
- Backend/API domain: `https://runtracer.com`
- Frontend should call: `https://runtracer.com/api`

---

## 7) ESP32-CAM integration strategy

Recommended path:
1. Keep ESP32-CAM streaming MJPEG on LAN (private IP).
2. Spring Boot backend proxies/controls access (auth, throttling, logging).
3. Angular app only talks to backend (`runtracer.com`), never directly to ESP32 in production.

This replaces the stock ESP32 UI while keeping firmware changes minimal.

---


## 8) Cloudflare Tunnel routing (important clarification)

Your understanding is close, but the key detail is:

- Tunnel ingress matches **hostname**.
- `service:` must point to an **origin root** like `http://127.0.0.1:8080` (or `:80`).
- The **request path is preserved** and forwarded to origin.

So yes, you can call endpoints such as:
- `https://runtracer.com/api/health`
- `https://runtracer.com/api/camera/session/start`

as long as the hostname route points to your Spring Boot origin and your app exposes those paths.

### 8.1 Example local managed config (`~/.cloudflared/config.yml`)

```yaml
tunnel: <TUNNEL_UUID>
credentials-file: /home/<user>/.cloudflared/<TUNNEL_UUID>.json

ingress:
  - hostname: runtracer.com
    service: http://127.0.0.1:8080
  - hostname: cam.runtracer.com
    service: http://192.168.0.175:80
  - service: http_status:404
```

Notes:
- Do **not** put `/api` or `/status` inside `service:` URLs.
- Cloudflared does not use per-path `service` URLs; paths are part of client request.
- If you run **token mode** (`cloudflared tunnel run --token ...`), ingress is managed in Cloudflare dashboard, not local `config.yml`.

### 8.2 Validate tunnel + endpoints

```bash
curl -sS -o /dev/null -w "%{http_code}\n" https://runtracer.com/
curl -sS -o /dev/null -w "%{http_code}\n" https://runtracer.com/api/health
curl -sS -o /dev/null -w "%{http_code}\n" https://cam.runtracer.com/status
```


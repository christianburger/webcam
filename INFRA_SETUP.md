# ESP32-CAM Relay Infrastructure Setup (Angular + Spring Boot)

This guide replaces direct use of the ESP32-CAM web interface with a custom stack:

- **Backend:** Java Spring Boot API/gateway
- **Frontend:** Angular SPA
- **Device:** ESP32-CAM on local network

## 1) Install base infrastructure

### Linux/macOS (recommended)
1. Install Java 21.
2. Install Maven 3.9+.
3. Install Node.js 20 LTS + npm.
4. Install Angular CLI (`npm i -g @angular/cli`).
5. (Optional) Install Docker + Docker Compose for local services.

### Windows
Use Winget/Chocolatey to install the same toolchain:
- OpenJDK 21
- Maven
- Node.js LTS
- Angular CLI
- Optional Docker Desktop

## 2) Create folders for backend and frontend

From repository root:

```bash
mkdir -p backend frontend
```

## 3) Bootstrap backend (Spring Boot)

Option A (recommended):
- Generate project from https://start.spring.io with:
  - Project: Maven
  - Language: Java
  - Spring Boot: latest stable
  - Dependencies: Spring Web, Spring WebSocket, Actuator
- Put generated project in `backend/relay-backend`.

Option B (CLI-only starter):

```bash
cd backend
mvn -N io.takari:maven:wrapper
```

Then scaffold or initialize a Spring Boot project using your preferred template.

## 4) Bootstrap frontend (Angular SPA)

```bash
cd frontend
npx @angular/cli@latest new relay-frontend --routing --style=scss
```

## 5) Connect frontend to backend

In Angular environment config:
- `apiBaseUrl = http://localhost:8080/api`

In Spring Boot:
- Enable CORS for `http://localhost:4200`.
- Implement REST endpoints for camera control and stream metadata.

## 6) Suggested local run workflow

Terminal 1 (backend):
```bash
cd backend/relay-backend
./mvnw spring-boot:run
```

Terminal 2 (frontend):
```bash
cd frontend/relay-frontend
npm install
npm start
```

## 7) ESP32-CAM integration strategy

Recommended approach:
1. Keep ESP32-CAM serving MJPEG stream on LAN.
2. Backend proxies/controls access (auth, rate limit, logging).
3. Angular consumes backend endpoints, not ESP32 directly.

This allows replacing the stock web interface while keeping device firmware simple.

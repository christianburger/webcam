# relay-backend (Spring Boot)

Spring Boot 3.5 gateway that serves the Angular SPA and proxies all camera
API calls to the ESP32 over the local network.

## Public model

```
https://runtracer.com/          → Angular SPA (static files)
https://runtracer.com/api/**    → relay endpoints → ESP32 LAN
```

The Cloudflare Tunnel points to `http://127.0.0.1:8080` on the host running
this service.

## Runtime requirements

- Java 21
- Maven 3.9+ (or use the included `./mvnw` wrapper)

## Configuration

All tunable values live in `src/main/resources/application.properties`:

```properties
app.public-base-url=https://runtracer.com
app.camera.resolve-ttl-seconds=60
app.camera.default-timeout-ms=5000

# Camera registry — add entries for each ESP32
app.camera.registry[0].id=cam-1
app.camera.registry[0].name=ESP32-CAM Front Door
app.camera.registry[0].host=web-cam.local
app.camera.registry[0].port=80
```

## Run locally

```bash
cd relay-backend
./mvnw spring-boot:run
```

Backend starts on `:8080`. Start the Angular dev server separately if you want
hot-reload on the frontend (`npm start` in `frontend/relay-frontend/`).

## Build (jar + embedded SPA)

```bash
cd relay-backend
./mvnw package          # builds Angular first, then packages the jar
```

The `frontend-maven-plugin` runs `npm install` + `ng build` in
`../frontend/relay-frontend/` and writes the output directly to
`src/main/resources/static/` before the jar is assembled.

To skip the frontend build (e.g. when iterating on Java only):

```bash
./mvnw package -Dskip.frontend=true
```

## Docker

Build and run from the **repo root** (`webcam/`):

```bash
# Build — context must be webcam/ so Docker can see both frontend/ and relay-backend/
docker build -t relay-backend -f relay-backend/Dockerfile .

# Run
docker run -p 8080:8080 relay-backend
```

The multi-stage `Dockerfile` reproduces what `./mvnw package` does locally:
Stage 1 (Node 22) builds the Angular SPA; Stage 2 (JDK 21) packages the Spring
Boot jar with the built SPA embedded; Stage 3 (JRE 21) is the minimal runtime
image.

## API endpoints

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/health` | Liveness check |
| `GET` | `/api/cameras` | List registered cameras |
| `GET` | `/api/cameras/{id}/status` | Relay `/status` from ESP32 |
| `POST` | `/api/cameras/{id}/session/start` | Mark session active |
| `POST` | `/api/cameras/{id}/session/stop` | Mark session inactive |
| `GET` | `/api/cameras/{id}/frame/latest` | Relay `/capture` as base64 JSON |
| `GET` | `/api/cameras/{id}/periph/state` | Relay `/periph/state` from ESP32 |
| `GET` | `/api/cameras/{id}/control/{name}` | Relay `/control/{name}?…` to ESP32 |

Angular client-side routes (`/dashboard`, `/camera/**`) are forwarded to
`index.html` by `SpaFallbackController`.

## Gentoo / Java install

```bash
sudo emerge --sync
sudo emerge -av dev-java/openjdk:21 dev-java/maven-bin
java -version   # 21.x
mvn -v          # 3.9.x
```

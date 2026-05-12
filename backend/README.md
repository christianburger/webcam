# Backend (Spring Boot)

Spring Boot is the gateway for both SPA delivery and camera API relay.

## Public model
- Single public URL: `https://runtracer.com`
- Tunnel points to backend: `http://127.0.0.1:8080`
- Backend serves:
  - `/` -> Angular static files
  - `/api/**` -> camera relay endpoints

## Runtime
- Java 21
- Maven 3.9+

## Gentoo install quick commands
```bash
sudo emerge --sync
sudo emerge -av dev-java/openjdk:21 dev-java/maven-bin
java -version
mvn -v
```

## Exact Spring Initializr bootstrap
Use these exact values:
- Group: `com.runtracer`
- Artifact/Name: `relay-backend`
- Package: `com.runtracer.relaybackend`
- Java: `21`
- Boot: `3.5.x` (latest stable on Initializr)
- Dependencies: `web, websocket, actuator, validation, security`

CLI download:
```bash
mkdir -p backend
curl -fsSL "https://start.spring.io/starter.zip?type=maven-project&language=java&bootVersion=3.5.0&baseDir=relay-backend&groupId=com.runtracer&artifactId=relay-backend&name=relay-backend&description=ESP32-CAM%20relay%20backend&packageName=com.runtracer.relaybackend&packaging=jar&javaVersion=21&dependencies=web,websocket,actuator,validation,security" -o /tmp/relay-backend.zip
unzip -q /tmp/relay-backend.zip -d backend
```

## Suggested endpoints
- `GET /api/cameras`
- `GET /api/cameras/{id}/status`
- `POST /api/cameras/{id}/session/start`
- `POST /api/cameras/{id}/session/stop`
- `GET /api/cameras/{id}/frame/latest`
- `GET /api/health`

## Relay responsibilities
- Resolve mDNS names to LAN IPs.
- Call ESP32 endpoints (`/status`, `/capture`, `/stream`).
- Relay normalized response payloads to SPA.

## Run
```bash
cd backend/relay-backend
./mvnw spring-boot:run
```

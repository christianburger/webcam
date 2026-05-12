# Backend (Spring Boot)

This folder contains the Spring Boot backend for the ESP32-CAM relay service.

## Target runtime
- Java 21
- Maven 3.9+
- Public base URL: `https://runtracer.com`

## Gentoo install quick commands
```bash
sudo emerge --sync
sudo emerge -av dev-java/openjdk:21 dev-java/maven
java -version
mvn -v
```

## Bootstrap project
Generate via Spring Initializr and place in:

```text
backend/relay-backend
```

Recommended dependencies:
- Spring Web
- Spring WebSocket
- Spring Boot Actuator
- Spring Security (optional)

## Suggested package layout
- `controller` (REST endpoints)
- `service` (camera relay/session logic)
- `config` (CORS/security/WebSocket)

## API base URLs
- Local: `http://localhost:8080`
- Production: `https://runtracer.com`

## First endpoints
- `POST /api/camera/session/start`
- `POST /api/camera/session/stop`
- `GET /api/camera/frame/latest`
- `GET /api/health`

## Run
```bash
cd backend/relay-backend
./mvnw spring-boot:run
```

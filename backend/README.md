# Backend (Spring Boot)

This folder will contain the Java Spring Boot backend for an ESP32-CAM relay service.

## Prerequisites
- Java 21 (LTS recommended)
- Maven 3.9+
- Optional: Docker + Docker Compose

## Bootstrap a Spring Boot app
From the repository root:

```bash
cd backend
mvn -N io.takari:maven:wrapper
./mvnw -q archetype:generate \
  -DgroupId=com.localrelay \
  -DartifactId=relay-backend \
  -DarchetypeArtifactId=maven-archetype-quickstart \
  -DinteractiveMode=false
```

Or generate a proper Spring Boot app from Spring Initializr with dependencies:
- Spring Web
- Spring Security (optional)
- Spring WebSocket
- Spring Boot Actuator

Then copy it into `backend/relay-backend`.

## Suggested structure
- `relay-backend/src/main/java/.../controller` (REST endpoints)
- `relay-backend/src/main/java/.../service` (ESP32 stream/session service)
- `relay-backend/src/main/java/.../config` (CORS, security, WebSocket)

## Suggested first endpoints
- `POST /api/camera/session/start`
- `POST /api/camera/session/stop`
- `GET /api/camera/frame/latest`
- `GET /api/health`

## Run backend
```bash
cd backend/relay-backend
./mvnw spring-boot:run
```

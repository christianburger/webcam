# Frontend (Angular SPA)

Angular 21 SPA served by Spring Boot at `/`. Source lives under
`frontend/relay-frontend/`.

## Pages

| Route | Component |
|---|---|
| `/dashboard` | Camera registry overview |
| `/camera/:id` | Live frame viewer, servo sliders, LED/switch controls |

## Single-origin integration

The Angular app uses a backend-relative API base URL so both assets and API
calls share one public domain:

```ts
// src/environments/environment.ts
export const environment = {
  production: false,
  apiBaseUrl: '/api'
};
```

Browser calls resolve as:
- `https://runtracer.com/` → Angular SPA (served by Spring Boot)
- `https://runtracer.com/api/…` → relay endpoints (proxied to ESP32)

## Camera registry

`public/cameras.json` seeds the camera list when the backend registry is
unavailable. Format:

```json
{
  "ESP32-CAM Front Door": "http://web-cam.local/"
}
```

`CameraApiService` tries (in priority order): `cameras.json` → `/api/cameras`
→ hardcoded default.

## Development server

```bash
cd frontend/relay-frontend
npm install
npm start          # ng serve — hot-reload on :4200
```

API calls to `/api` are proxied to `http://localhost:8080` via
`proxy.conf.json`.

## Production build

Angular CLI outputs directly into the Spring Boot static directory:

```
relay-backend/src/main/resources/static/
```

Triggered automatically by `./mvnw package` (via `frontend-maven-plugin`) or
by Docker. To build manually:

```bash
cd frontend/relay-frontend
npm run build      # ng build --configuration production
```

## Requirements

- Node.js 22 + npm 11
- Angular CLI 21 (`npm install` installs it locally; no global install needed)

# relay-frontend

Angular 21 SPA for the Relay Camera Console. Generated with Angular CLI 21.2.10.

## Quick start

```bash
npm install
npm start          # ng serve — http://localhost:4200
```

API calls to `/api` are proxied to `http://localhost:8080` (Spring Boot) by
`proxy.conf.json`. Start the backend first or the camera controls will error.

## Structure

```
src/app/
├── app.ts / app.html / app.scss   # Root shell + topbar
├── app.routes.ts                  # /dashboard, /camera/:id
├── dashboard/
│   ├── dashboard.component.ts
│   └── components/
│       ├── camera-list.component.ts
│       └── dashboard-hero.component.ts
├── camera/
│   └── camera.component.ts        # Frame viewer + controls
├── services/
│   └── camera-api.service.ts      # All /api/* HTTP calls
├── models/
│   └── camera-models.ts           # Shared TS interfaces
└── interceptors/
    └── logging.interceptor.ts     # Dev-mode HTTP logging
```

## Available scripts

| Command | Description |
|---|---|
| `npm start` | Dev server with hot-reload (`ng serve`) |
| `npm run build` | Production build → `../../relay-backend/src/main/resources/static` |
| `npm run watch` | Dev build, rebuild on change |
| `npm test` | Unit tests via Vitest |

## Production build output

`angular.json` routes the production build directly into Spring Boot's static
directory so the jar serves the SPA without an extra copy step:

```
relay-backend/src/main/resources/static/
```

## Proxy configuration

`proxy.conf.json` forwards `/api` to the backend during `ng serve`:

```json
{ "/api": { "target": "http://localhost:8080", "changeOrigin": true } }
```

In production, both SPA and API share the same origin (`localhost:8080` /
`runtracer.com`), so no proxy is needed.

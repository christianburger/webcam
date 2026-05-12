# Frontend (Angular SPA)

This folder will contain the Angular single-page application for camera control and viewing.

## Prerequisites
- Node.js 20 LTS
- npm 10+
- Angular CLI 19+

## Bootstrap Angular app
From repository root:

```bash
cd frontend
npx @angular/cli@latest new relay-frontend --routing --style=scss
```

When prompted:
- Enable SSR: No (for now)
- Zone.js: Yes (default)

## Suggested modules/pages
- `camera-view` (live feed panel)
- `camera-control` (start/stop, quality, flash)
- `settings` (backend URL, credentials)

## Environment config
Create `src/environments/environment.ts` with backend URL:

```ts
export const environment = {
  production: false,
  apiBaseUrl: 'http://localhost:8080/api'
};
```

## Run frontend
```bash
cd frontend/relay-frontend
npm install
npm start
```

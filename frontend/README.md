# Frontend (Angular SPA)

Angular SPA is the user UI for selecting cameras and issuing control actions.

## Single-origin integration
Use backend-relative API base URL so frontend and backend stay under one public domain:

```ts
export const environment = {
  production: false,
  apiBaseUrl: '/api'
};
```

With this, browser calls:
- `https://runtracer.com/` for SPA
- `https://runtracer.com/api/...` for backend API

## Requirements
- Node.js + npm
- Angular CLI

## Create app
```bash
cd frontend
npx @angular/cli@latest new relay-frontend --routing --style=scss
```

## Suggested pages
- camera selector
- camera controls
- live stream/frame view
- session/status panel

## Run
```bash
cd frontend/relay-frontend
npm install
npm start
```

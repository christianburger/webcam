# Frontend (Angular SPA)

This folder contains the Angular SPA for camera viewing and control.

## Requirements (Gentoo)
- Node.js + npm (`net-libs/nodejs`)
- Angular CLI (`npm i -g @angular/cli`)

Quick check:
```bash
node -v
npm -v
ng version
```

## Create app
```bash
cd frontend
npx @angular/cli@latest new relay-frontend --routing --style=scss
```

## Environment configuration
Use local API for development:

`src/environments/environment.ts`
```ts
export const environment = {
  production: false,
  apiBaseUrl: 'http://localhost:8080/api'
};
```

Use runtracer domain for production:

`src/environments/environment.prod.ts`
```ts
export const environment = {
  production: true,
  apiBaseUrl: 'https://runtracer.com/api'
};
```

## Suggested SPA areas
- `camera-view` (live feed)
- `camera-control` (start/stop/quality/flash)
- `settings` (API URL/auth)

## Run
```bash
cd frontend/relay-frontend
npm install
npm start
```

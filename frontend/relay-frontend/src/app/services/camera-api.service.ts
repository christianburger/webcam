import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable, catchError, map, of, forkJoin, tap } from 'rxjs';
import { environment } from '../../environments/environment';
import { CameraStatus, CameraSummary, FramePayload, SessionState } from '../models/camera-models';

const DEFAULT_CAMERAS: CameraSummary[] = [
  { id: 'cam-1', name: 'ESP32-CAM Front Door', host: 'web-cam.local', port: 80, uri: 'http://web-cam.local/' }
];

function fromObjectMap(value: unknown): CameraSummary[] {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return [];
  return Object.entries(value as Record<string, string>)
    .filter(([, uri]) => typeof uri === 'string' && uri.length > 0)
    .map(([name, uri], i) => {
      let host = 'web-cam.local'; let port = 80;
      try { const u = new URL(uri); host = u.hostname; port = Number(u.port || '80'); } catch {}
      return { id: `cam-${i + 1}`, name, host, port, uri };
    });
}

function asCameraList(value: unknown): CameraSummary[] {
  if (Array.isArray(value)) {
    return (value as any[])
      .filter(c => c && typeof c.id === 'string' && typeof c.host === 'string')
      .map(c => ({ ...c, uri: c.uri || `http://${c.host}:${c.port || 80}/` }));
  }
  return fromObjectMap(value);
}

@Injectable({ providedIn: 'root' })
export class CameraApiService {
  private readonly api = environment.apiBaseUrl;

  constructor(private http: HttpClient) {
    console.log('[CameraApiService] initialized. apiBaseUrl =', `"${this.api}"`);
    console.log('[CameraApiService] full status URL example:',
      `${this.api}/cameras/cam-1/status`);
  }

  cameras(): Observable<CameraSummary[]> {
    console.log('[cameras] starting forkJoin fetch...');
    console.log('[cameras]   backend  →', `${this.api}/cameras`);
    console.log('[cameras]   local    → /cameras.json');

    const backend$ = this.http.get<unknown>(`${this.api}/cameras`).pipe(
      tap(v  => console.log('[cameras] backend raw:', v)),
      map(asCameraList),
      tap(v  => console.log('[cameras] backend parsed:', v)),
      catchError(err => {
        console.warn('[cameras] backend error:', err.status, err.message);
        return of([] as CameraSummary[]);
      })
    );

    const localAbs$ = this.http.get<unknown>('/cameras.json').pipe(
      tap(v  => console.log('[cameras] /cameras.json raw:', v)),
      map(asCameraList),
      tap(v  => console.log('[cameras] /cameras.json parsed:', v)),
      catchError(err => {
        console.warn('[cameras] /cameras.json error:', err.status, err.message);
        return of([] as CameraSummary[]);
      })
    );

    const localRel$ = this.http.get<unknown>('cameras.json').pipe(
      map(asCameraList),
      catchError(() => of([] as CameraSummary[]))
    );

    return forkJoin([backend$, localAbs$, localRel$]).pipe(
      map(([backend, abs, rel]) => {
        const local = abs.length ? abs : rel;
        console.log('[cameras] forkJoin done — backend=%d local=%d', backend.length, local.length);
        if (local.length > 0) {
          console.log('[cameras] ✔ using cameras.json:', local);
          return local;
        }
        if (backend.length > 0) {
          console.log('[cameras] ✔ using backend registry:', backend);
          return backend;
        }
        console.warn('[cameras] ⚠ both sources empty — using DEFAULT_CAMERAS');
        return DEFAULT_CAMERAS;
      }),
      catchError(err => {
        console.error('[cameras] forkJoin failed:', err);
        return of(DEFAULT_CAMERAS);
      })
    );
  }

  status(id: string): Observable<CameraStatus> {
    const url = `${this.api}/cameras/${id}/status`;
    console.log('[status] GET', url);
    return this.http.get<CameraStatus>(url).pipe(
      tap(v  => console.log('[status] response:', v)),
      catchError(err => {
        console.error('[status] ERROR:', err.status, err.message);
        throw err;
      })
    );
  }

  start(id: string): Observable<SessionState> {
    const url = `${this.api}/cameras/${id}/session/start`;
    console.log('[session] POST', url);
    return this.http.post<SessionState>(url, {}).pipe(
      tap(v  => console.log('[session] start response:', v))
    );
  }

  stop(id: string): Observable<SessionState> {
    const url = `${this.api}/cameras/${id}/session/stop`;
    console.log('[session] POST', url);
    return this.http.post<SessionState>(url, {}).pipe(
      tap(v  => console.log('[session] stop response:', v))
    );
  }

  frame(id: string): Observable<FramePayload> {
    const url = `${this.api}/cameras/${id}/frame/latest`;
    console.log('[frame] GET', url);
    return this.http.get<FramePayload>(url).pipe(
      tap(v => {
        if (v.base64)
          console.log('[frame] ✔ image received: %d base64 chars, type=%s', v.base64.length, v.contentType);
        else
          console.warn('[frame] ✗ no image. error=', (v as any).error);
      }),
      catchError(err => {
        console.error('[frame] ERROR:', err.status, err.message);
        throw err;
      })
    );
  }

  periph(id: string): Observable<any> {
    const url = `${this.api}/cameras/${id}/periph/state`;
    console.log('[periph] GET', url);
    return this.http.get<any>(url).pipe(
      tap(v  => console.log('[periph] response:', v))
    );
  }

  control(id: string, name: string, params: string): Observable<any> {
    const url = `${this.api}/cameras/${id}/control/${name}?${params}`;
    console.log('[control] GET', url);
    return this.http.get<any>(url).pipe(
      tap(v  => console.log('[control] response:', v)),
      catchError(err => {
        console.error('[control] ERROR:', err.status, err.message);
        throw err;
      })
    );
  }
}

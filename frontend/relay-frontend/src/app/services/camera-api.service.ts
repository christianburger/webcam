import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable, catchError, map, of, forkJoin } from 'rxjs';
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
    return value
      .filter((c: any) => c && typeof c.id === 'string' && typeof c.host === 'string')
      .map((c: any) => ({ ...c, uri: c.uri || `http://${c.host}:${c.port || 80}/` }));
  }
  return fromObjectMap(value);
}

@Injectable({ providedIn: 'root' })
export class CameraApiService {
  private readonly api = environment.apiBaseUrl;
  constructor(private http: HttpClient) {}

  cameras(): Observable<CameraSummary[]> {
    const backend$ = this.http.get<unknown>(`${this.api}/cameras`).pipe(map(asCameraList), catchError(() => of([])));
    const localAbs$ = this.http.get<unknown>('/cameras.json').pipe(map(asCameraList), catchError(() => of([])));
    const localRel$ = this.http.get<unknown>('cameras.json').pipe(map(asCameraList), catchError(() => of([])));

    return forkJoin([backend$, localAbs$, localRel$]).pipe(
      map(([backend, abs, rel]) => {
        const local = abs.length ? abs : rel;
        if (local.length > 0) return local;
        if (backend.length > 0) return backend;
        return DEFAULT_CAMERAS;
      }),
      catchError(() => of(DEFAULT_CAMERAS))
    );
  }

  status(id: string): Observable<CameraStatus> { return this.http.get<CameraStatus>(`${this.api}/cameras/${id}/status`); }
  start(id: string): Observable<SessionState> { return this.http.post<SessionState>(`${this.api}/cameras/${id}/session/start`, {}); }
  stop(id: string): Observable<SessionState> { return this.http.post<SessionState>(`${this.api}/cameras/${id}/session/stop`, {}); }
  frame(id: string): Observable<FramePayload> { return this.http.get<FramePayload>(`${this.api}/cameras/${id}/frame/latest`); }
  periph(id: string): Observable<any> { return this.http.get<any>(`${this.api}/cameras/${id}/periph/state`); }
  control(id: string, name: string, params: string): Observable<any> { return this.http.get<any>(`${this.api}/cameras/${id}/control/${name}?${params}`); }
}


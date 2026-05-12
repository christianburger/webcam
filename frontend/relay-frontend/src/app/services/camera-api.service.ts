import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable, catchError, map, of, forkJoin } from 'rxjs';
import { environment } from '../../environments/environment';
import { CameraStatus, CameraSummary, FramePayload, SessionState } from '../models/camera-models';

const DEFAULT_CAMERAS: CameraSummary[] = [
  { id: 'cam-1', name: 'ESP32-CAM Front Door', host: 'web-cam.local', port: 80 }
];

function asCameraList(value: unknown): CameraSummary[] {
  if (!Array.isArray(value)) return [];
  return value.filter((c: any) => c && typeof c.id === 'string' && typeof c.host === 'string');
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
        if (backend.length > 0) return backend;
        if (local.length > 0) return local;
        return DEFAULT_CAMERAS;
      }),
      catchError(() => of(DEFAULT_CAMERAS))
    );
  }

  status(id: string): Observable<CameraStatus> { return this.http.get<CameraStatus>(`${this.api}/cameras/${id}/status`); }
  start(id: string): Observable<SessionState> { return this.http.post<SessionState>(`${this.api}/cameras/${id}/session/start`, {}); }
  stop(id: string): Observable<SessionState> { return this.http.post<SessionState>(`${this.api}/cameras/${id}/session/stop`, {}); }
  frame(id: string): Observable<FramePayload> { return this.http.get<FramePayload>(`${this.api}/cameras/${id}/frame/latest`); }
}

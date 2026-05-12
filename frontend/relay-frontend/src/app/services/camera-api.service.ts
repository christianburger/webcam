import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable, catchError, map, of, forkJoin } from 'rxjs';
import { environment } from '../../environments/environment';
import { CameraStatus, CameraSummary, FramePayload, SessionState } from '../models/camera-models';

const DEFAULT_CAMERAS: CameraSummary[] = [
  { id: 'cam-1', name: 'ESP32-CAM Front Door', host: 'web-cam.local', port: 80 }
];

@Injectable({ providedIn: 'root' })
export class CameraApiService {
  private readonly api = environment.apiBaseUrl;
  constructor(private http: HttpClient) {}

  cameras(): Observable<CameraSummary[]> {
    const backend$ = this.http.get<CameraSummary[]>(`${this.api}/cameras`).pipe(catchError(() => of([])));
    const localAbs$ = this.http.get<CameraSummary[]>('/cameras.json').pipe(catchError(() => of([])));
    const localRel$ = this.http.get<CameraSummary[]>('cameras.json').pipe(catchError(() => of([])));
    return forkJoin([backend$, localAbs$, localRel$]).pipe(
      map(([backend, abs, rel]) => {
        const local = abs.length ? abs : rel;
        if (backend.length) return backend;
        if (local.length) return local;
        return DEFAULT_CAMERAS;
      })
    );
  }

  status(id: string): Observable<CameraStatus> { return this.http.get<CameraStatus>(`${this.api}/cameras/${id}/status`); }
  start(id: string): Observable<SessionState> { return this.http.post<SessionState>(`${this.api}/cameras/${id}/session/start`, {}); }
  stop(id: string): Observable<SessionState> { return this.http.post<SessionState>(`${this.api}/cameras/${id}/session/stop`, {}); }
  frame(id: string): Observable<FramePayload> { return this.http.get<FramePayload>(`${this.api}/cameras/${id}/frame/latest`); }
}

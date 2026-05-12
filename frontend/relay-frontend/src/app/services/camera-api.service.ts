import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable, catchError, map, of, forkJoin } from 'rxjs';
import { environment } from '../../environments/environment';
import { CameraStatus, CameraSummary, FramePayload, SessionState } from '../models/camera-models';

@Injectable({ providedIn: 'root' })
export class CameraApiService {
  private readonly api = environment.apiBaseUrl;
  constructor(private http: HttpClient) {}

  cameras(): Observable<CameraSummary[]> {
    const backend$ = this.http.get<CameraSummary[]>(`${this.api}/cameras`).pipe(catchError(() => of([])));
    const local$ = this.http.get<CameraSummary[]>('/cameras.json').pipe(catchError(() => of([])));
    return forkJoin([backend$, local$]).pipe(
      map(([backend, local]) => backend.length > 0 ? backend : local)
    );
  }

  status(id: string): Observable<CameraStatus> { return this.http.get<CameraStatus>(`${this.api}/cameras/${id}/status`); }
  start(id: string): Observable<SessionState> { return this.http.post<SessionState>(`${this.api}/cameras/${id}/session/start`, {}); }
  stop(id: string): Observable<SessionState> { return this.http.post<SessionState>(`${this.api}/cameras/${id}/session/stop`, {}); }
  frame(id: string): Observable<FramePayload> { return this.http.get<FramePayload>(`${this.api}/cameras/${id}/frame/latest`); }
}

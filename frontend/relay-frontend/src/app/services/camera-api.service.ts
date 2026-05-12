import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable } from 'rxjs';
import { environment } from '../../environments/environment';
import { CameraStatus, CameraSummary, SessionState } from '../models/camera-models';

@Injectable({ providedIn: 'root' })
export class CameraApiService {
  private readonly api = environment.apiBaseUrl;
  constructor(private http: HttpClient) {}
  cameras(): Observable<CameraSummary[]> { return this.http.get<CameraSummary[]>(`${this.api}/cameras`); }
  status(id: string): Observable<CameraStatus> { return this.http.get<CameraStatus>(`${this.api}/cameras/${id}/status`); }
  start(id: string): Observable<SessionState> { return this.http.post<SessionState>(`${this.api}/cameras/${id}/session/start`, {}); }
  stop(id: string): Observable<SessionState> { return this.http.post<SessionState>(`${this.api}/cameras/${id}/session/stop`, {}); }
}

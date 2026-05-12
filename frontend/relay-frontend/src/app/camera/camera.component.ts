import { Component, OnInit } from '@angular/core';
import { ActivatedRoute, RouterLink } from '@angular/router';
import { JsonPipe, NgIf } from '@angular/common';
import { CameraApiService } from '../services/camera-api.service';
import { CameraStatus, CameraSummary, SessionState } from '../models/camera.models';

@Component({
  standalone: true,
  imports: [NgIf, JsonPipe, RouterLink],
  template: `
    <main>
      <a routerLink="/dashboard">← back to #dashboard</a>
      <h2>Camera {{cameraId}}</h2>
      <p *ngIf="camera"><b>Relay details:</b> {{camera.name}} → {{camera.host}}:{{camera.port}}</p>
      <div class="buttons"><button (click)="refreshStatus()">Status</button><button (click)="start()">Start session</button><button (click)="stop()">Stop session</button></div>
      <h3>Last Status</h3><pre>{{status | json}}</pre>
      <h3>Session</h3><pre>{{session | json}}</pre>
    </main>
  `,
  styles: ['main{padding:1rem}.buttons{display:flex;gap:8px;margin:10px 0}pre{background:#1b1b1b;padding:8px;border-radius:6px}']
})
export class CameraComponent implements OnInit {
  cameraId = '';
  camera?: CameraSummary;
  status?: CameraStatus;
  session?: SessionState;
  constructor(private route: ActivatedRoute, private api: CameraApiService) {}
  ngOnInit(): void {
    this.cameraId = this.route.snapshot.paramMap.get('id') || '';
    this.api.cameras().subscribe(c => this.camera = c.find(x => x.id === this.cameraId));
    this.refreshStatus();
  }
  refreshStatus(): void { this.api.status(this.cameraId).subscribe(v => this.status = v); }
  start(): void { this.api.start(this.cameraId).subscribe(v => this.session = v); }
  stop(): void { this.api.stop(this.cameraId).subscribe(v => this.session = v); }
}

import { Component, OnDestroy, OnInit } from '@angular/core';
import { ActivatedRoute, RouterLink } from '@angular/router';
import { NgIf } from '@angular/common';
import { CameraApiService } from '../services/camera-api.service';
import { CameraStatus, CameraSummary, SessionState } from '../models/camera-models';

@Component({
  standalone: true,
  imports: [NgIf, RouterLink],
  template: `
    <main class="camera-page">
      <a routerLink="/dashboard">← back to dashboard</a>
      <h2>{{camera?.name || ('Camera ' + cameraId)}}</h2>
      <p class="meta">mDNS: {{camera?.host || 'web-cam.local'}} · Port: {{camera?.port || 80}}</p>

      <section class="viewer">
        <img *ngIf="frameSrc; else noframe" [src]="frameSrc" alt="Latest frame" />
        <ng-template #noframe><p class="frame-msg">{{frameMessage}}</p></ng-template>
      </section>

      <section class="controls">
        <button (click)="refreshStatus()">Refresh status</button>
        <button (click)="refreshFrame()">Capture frame</button>
        <button (click)="start()">Start session</button>
        <button (click)="stop()">Stop session</button>
      </section>

      <section class="controls">
        <label>Pan <input type="range" min="0" max="180" [value]="pan" (input)="setServo('pan',$event)" /></label>
        <label>Tilt <input type="range" min="0" max="180" [value]="tilt" (input)="setServo('tilt',$event)" /></label>
        <button (click)="toggleSwitch()">Toggle Switch</button>
        <button (click)="toggleLed()">Toggle LED</button>
      </section>

      <pre *ngIf="status">{{status.body}}</pre>
      <pre *ngIf="session">Session active: {{session.active}} @ {{session.changedAt}}</pre>
    </main>
  `,
  styles: ['.camera-page{padding:1.25rem}.meta{color:#94a3b8}.viewer{margin:1rem 0;background:#020617;border:1px solid #334155;border-radius:12px;padding:.75rem;min-height:240px;display:flex;align-items:center;justify-content:center}img{width:100%;max-width:820px;border-radius:10px;display:block;margin:auto;box-shadow:0 10px 25px rgba(2,6,23,.5)}.controls{display:flex;gap:.5rem;flex-wrap:wrap;align-items:center;margin:1rem 0}button{background:#0ea5e9;border:none;color:#082f49;padding:.45rem .8rem;border-radius:8px;font-weight:700;cursor:pointer}label{display:flex;gap:.5rem;align-items:center;background:#1e293b;padding:.3rem .5rem;border-radius:8px}pre{background:#020617;border:1px solid #334155;padding:.75rem;border-radius:8px;color:#cbd5e1}.frame-msg{padding:2rem 1rem;text-align:center;color:#94a3b8}']
})
export class CameraComponent implements OnInit, OnDestroy {
  cameraId = '';
  camera?: CameraSummary;
  status?: CameraStatus;
  session?: SessionState;
  frameSrc = '';
  frameMessage = 'No frame yet. Camera may be offline.';
  pan = 90;
  tilt = 90;
  ledOn = false;
  switchOn = false;
  private frameTimer?: ReturnType<typeof setInterval>;

  constructor(private route: ActivatedRoute, private api: CameraApiService) {}

  ngOnInit(): void {
    this.cameraId = this.route.snapshot.fragment || 'cam-1';
    this.api.cameras().subscribe(c => {
      this.camera = c.find(x => x.id === this.cameraId) || { id: this.cameraId, name: `Camera ${this.cameraId}`, host: 'web-cam.local', port: 80, uri: 'http://web-cam.local/' };
      this.refreshFrame();
      this.frameTimer = setInterval(() => this.refreshFrame(), 2000);
    });
    this.refreshStatus();
  }

  ngOnDestroy(): void { if (this.frameTimer) clearInterval(this.frameTimer); }
  refreshStatus(): void { this.api.status(this.cameraId).subscribe(v => this.status = v); }
  refreshFrame(): void {
    this.api.frame(this.cameraId).subscribe(v => {
      if (v.base64) { this.frameSrc = `data:${v.contentType};base64,${v.base64}`; this.frameMessage = `Frame updated at ${v.capturedAt}`; }
      else { this.frameSrc = ''; this.frameMessage = v.error || 'Camera reachable but no frame payload.'; }
    });
  }
  start(): void { this.api.start(this.cameraId).subscribe(v => this.session = v); }
  stop(): void { this.api.stop(this.cameraId).subscribe(v => this.session = v); }

  setServo(axis: 'pan' | 'tilt', event: Event): void {
    const value = Number((event.target as HTMLInputElement).value);
    if (axis === 'pan') this.pan = value; else this.tilt = value;
    this.api.control(this.cameraId, axis, `angle=${value}`).subscribe();
  }
  toggleLed(): void { this.ledOn = !this.ledOn; this.api.control(this.cameraId, 'led', `state=${this.ledOn ? 1 : 0}`).subscribe(); }
  toggleSwitch(): void { this.switchOn = !this.switchOn; this.api.control(this.cameraId, 'switch', `state=${this.switchOn ? 1 : 0}`).subscribe(); }
}

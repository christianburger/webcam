import { Component, OnInit } from '@angular/core';
import { ActivatedRoute, RouterLink } from '@angular/router';
import { NgIf } from '@angular/common';
import { CameraApiService } from '../services/camera-api.service';
import { CameraSummary } from '../models/camera-models';
import { environment } from '../../environments/environment';

@Component({
  standalone: true,
  imports: [NgIf, RouterLink],
  template: `
    <main class="camera-page">
      <a routerLink="/dashboard" class="back-link">← Dashboard</a>
      <h2>{{ camera?.name || ('Camera ' + cameraId) }}</h2>
      <p class="meta">{{ camera?.host || 'web-cam.local' }}:{{ camera?.port || 80 }}</p>

      <!-- ── Live stream viewer ───────────────────────────────────────────── -->
      <section class="viewer">
        <img
          [src]="streamUrl"
          (error)="onStreamError()"
          alt="Live MJPEG stream"
          class="stream-img"
        />
        <div *ngIf="streamError" class="stream-overlay">
          <p>Stream offline — camera may be unreachable</p>
          <button (click)="retryStream()" class="btn-retry">↺ Retry</button>
        </div>
      </section>

      <!-- ── Camera actions ──────────────────────────────────────────────── -->
      <section class="actions">
        <button class="btn" (click)="openCapture()">📸 Capture</button>
        <button class="btn btn-muted" disabled title="Coming soon">⏺ Record</button>
      </section>

      <!-- ── Peripheral controls ─────────────────────────────────────────── -->
      <section class="card">
        <h3 class="card-title">Peripherals</h3>

        <div class="periph-row">
          <span class="periph-name">LED</span>
          <button
            class="btn-toggle"
            [class.active]="ledOn === true"
            (click)="setLed(true)">ON</button>
          <button
            class="btn-toggle"
            [class.active]="ledOn === false"
            (click)="setLed(false)">OFF</button>
        </div>

        <div class="periph-row">
          <span class="periph-name">Relay</span>
          <button
            class="btn-toggle"
            [class.active]="relayOn === true"
            (click)="setRelay(true)">ON</button>
          <button
            class="btn-toggle"
            [class.active]="relayOn === false"
            (click)="setRelay(false)">OFF</button>
        </div>
      </section>

      <!-- ── Pan / Tilt ───────────────────────────────────────────────────── -->
      <section class="card">
        <h3 class="card-title">Pan / Tilt</h3>

        <label class="slider-row">
          <span class="slider-label">Pan</span>
          <input
            type="range" min="0" max="180"
            [value]="pan"
            (input)="setServo('pan', $event)" />
          <span class="angle-val">{{ pan }}°</span>
        </label>

        <label class="slider-row">
          <span class="slider-label">Tilt</span>
          <input
            type="range" min="0" max="180"
            [value]="tilt"
            (input)="setServo('tilt', $event)" />
          <span class="angle-val">{{ tilt }}°</span>
        </label>
      </section>
    </main>
  `,
  styles: [`
    /* ── Layout ────────────────────────────────────────────────────────────── */
    :host { display: block; }
    .camera-page {
      padding: 1.25rem;
      display: grid;
      gap: 1rem;
      max-width: 900px;
    }

    .back-link { color: #7dd3fc; text-decoration: none; font-size: .875rem; }
    .back-link:hover { color: #38bdf8; }
    h2 { margin: .2rem 0 0; font-size: 1.35rem; }
    .meta { margin: 0; color: #94a3b8; font-size: .825rem; }

    /* ── Viewer ─────────────────────────────────────────────────────────────── */
    .viewer {
      position: relative;
      background: #020617;
      border: 1px solid #334155;
      border-radius: 12px;
      overflow: hidden;
      min-height: 260px;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .stream-img {
      width: 100%;
      max-width: 820px;
      display: block;
      border-radius: 12px;
    }
    .stream-overlay {
      position: absolute;
      inset: 0;
      background: rgba(2, 6, 23, .92);
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      gap: .75rem;
      color: #94a3b8;
      font-size: .9rem;
    }
    .btn-retry {
      background: #1e293b;
      border: 1px solid #334155;
      color: #94a3b8;
      padding: .4rem .9rem;
      border-radius: 8px;
      cursor: pointer;
      font-size: .875rem;
    }
    .btn-retry:hover { border-color: #0ea5e9; color: #7dd3fc; }

    /* ── Actions ────────────────────────────────────────────────────────────── */
    .actions { display: flex; gap: .5rem; flex-wrap: wrap; }
    .btn {
      background: #0ea5e9;
      border: none;
      color: #082f49;
      padding: .5rem 1.1rem;
      border-radius: 8px;
      font-weight: 700;
      cursor: pointer;
      font-size: .9rem;
    }
    .btn:hover { background: #38bdf8; }
    .btn-muted {
      background: #1e293b;
      color: #475569;
      cursor: not-allowed;
    }

    /* ── Card ───────────────────────────────────────────────────────────────── */
    .card {
      background: #111827;
      border: 1px solid #334155;
      border-radius: 12px;
      padding: 1rem 1.1rem;
      display: grid;
      gap: .6rem;
    }
    .card-title {
      margin: 0 0 .2rem;
      font-size: .8rem;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: .06em;
      color: #64748b;
    }

    /* ── Peripheral toggles ─────────────────────────────────────────────────── */
    .periph-row { display: flex; align-items: center; gap: .5rem; }
    .periph-name { min-width: 55px; color: #e2e8f0; font-size: .9rem; }
    .btn-toggle {
      background: #0f172a;
      border: 1px solid #334155;
      color: #64748b;
      padding: .3rem .8rem;
      border-radius: 6px;
      cursor: pointer;
      font-size: .85rem;
      transition: background .15s, color .15s, border-color .15s;
    }
    .btn-toggle:hover { border-color: #0ea5e9; color: #7dd3fc; }
    .btn-toggle.active {
      background: #0ea5e9;
      border-color: #0ea5e9;
      color: #082f49;
      font-weight: 700;
    }

    /* ── Sliders ────────────────────────────────────────────────────────────── */
    .slider-row {
      display: flex;
      align-items: center;
      gap: .75rem;
      color: #e2e8f0;
      font-size: .9rem;
    }
    .slider-label { min-width: 40px; }
    .slider-row input[type=range] { flex: 1; accent-color: #0ea5e9; cursor: pointer; }
    .angle-val { min-width: 38px; text-align: right; color: #94a3b8; font-size: .825rem; }
  `]
})
export class CameraComponent implements OnInit {
  cameraId = '';
  camera?: CameraSummary;

  streamUrl = '';
  streamError = false;

  pan  = 90;
  tilt = 90;

  ledOn   = false;
  relayOn = false;

  constructor(private route: ActivatedRoute, private api: CameraApiService) {}

  ngOnInit(): void {
    this.cameraId = this.route.snapshot.paramMap.get('id') || 'cam-1';

    this.api.cameras().subscribe({
      next: cameras =>
        (this.camera = cameras.find(c => c.id === this.cameraId) ?? {
          id: this.cameraId,
          name: `Camera ${this.cameraId}`,
          host: 'web-cam.local',
          port: 80,
          uri: 'http://web-cam.local/'
        })
    });

    this.streamUrl = `${environment.apiBaseUrl}/cameras/${this.cameraId}/stream`;
  }

  // ── Stream ──────────────────────────────────────────────────────────────────

  onStreamError(): void {
    this.streamError = true;
  }

  retryStream(): void {
    this.streamError = false;
    // Bust the cache so the browser opens a fresh connection
    this.streamUrl =
      `${environment.apiBaseUrl}/cameras/${this.cameraId}/stream?t=${Date.now()}`;
  }

  // ── Capture / Record ────────────────────────────────────────────────────────

  openCapture(): void {
    window.open(
      `${environment.apiBaseUrl}/cameras/${this.cameraId}/capture`,
      '_blank'
    );
  }

  // ── Peripheral controls ─────────────────────────────────────────────────────

  setLed(on: boolean): void {
    this.ledOn = on;
    this.api.control(this.cameraId, 'led', `state=${on ? 1 : 0}`).subscribe({
      error: err => console.error('[CameraComponent] setLed error:', err)
    });
  }

  setRelay(on: boolean): void {
    this.relayOn = on;
    this.api.control(this.cameraId, 'switch', `state=${on ? 1 : 0}`).subscribe({
      error: err => console.error('[CameraComponent] setRelay error:', err)
    });
  }

  setServo(axis: 'pan' | 'tilt', event: Event): void {
    const value = Number((event.target as HTMLInputElement).value);
    if (axis === 'pan') this.pan = value; else this.tilt = value;
    this.api.control(this.cameraId, axis, `angle=${value}`).subscribe({
      error: err => console.error('[CameraComponent] setServo error:', err)
    });
  }
}

import { Component, OnInit } from '@angular/core';
import { CameraApiService } from '../services/camera-api.service';
import { CameraSummary } from '../models/camera-models';
import { DashboardHeroComponent } from './components/dashboard-hero.component';
import { CameraListComponent } from './components/camera-list.component';

const SEEDED_CAMERAS: CameraSummary[] = [
  { id: 'cam-1', name: 'ESP32-CAM Front Door', host: 'web-cam.local', port: 80, uri: 'http://web-cam.local/' }
];

@Component({
  standalone: true,
  imports: [DashboardHeroComponent, CameraListComponent],
  template: `
    <main class="dashboard">
      <app-dashboard-hero [total]="cameras.length" [online]="0"></app-dashboard-hero>

      <section class="section">
        <h3>Registered Cameras</h3>
        <app-camera-list [cameras]="cameras"></app-camera-list>
      </section>
    </main>
  `,
  styles: ['.dashboard{padding:1.25rem;display:grid;gap:1rem}.section h3{margin:.2rem 0 .75rem}']
})
export class DashboardComponent implements OnInit {
  cameras: CameraSummary[] = [...SEEDED_CAMERAS];
  constructor(private api: CameraApiService) {}
  ngOnInit(): void {
    this.api.cameras().subscribe(v => {
      if (v.length > 0) this.cameras = v;
    });
  }
}

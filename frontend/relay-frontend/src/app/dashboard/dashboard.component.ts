import { Component, OnInit } from '@angular/core';
import { NgIf } from '@angular/common';
import { CameraApiService } from '../services/camera-api.service';
import { CameraSummary } from '../models/camera-models';
import { DashboardHeroComponent } from './components/dashboard-hero.component';
import { CameraListComponent } from './components/camera-list.component';

@Component({
  standalone: true,
  imports: [NgIf, DashboardHeroComponent, CameraListComponent],
  template: `
    <main class="dashboard">
      <app-dashboard-hero [total]="cameras.length" [online]="0"></app-dashboard-hero>

      <section class="section">
        <h3>Registered Cameras</h3>
        <app-camera-list *ngIf="cameras.length; else empty" [cameras]="cameras"></app-camera-list>
        <ng-template #empty><p class="empty">No cameras returned by API or local registry.</p></ng-template>
      </section>
    </main>
  `,
  styles: ['.dashboard{padding:1.25rem;display:grid;gap:1rem}.section h3{margin:.2rem 0 .75rem}.empty{color:#fca5a5}']
})
export class DashboardComponent implements OnInit {
  cameras: CameraSummary[] = [];
  constructor(private api: CameraApiService) {}
  ngOnInit(): void { this.api.cameras().subscribe(v => this.cameras = v); }
}

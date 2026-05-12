import { Component, OnInit } from '@angular/core';
import { NgFor } from '@angular/common';
import { RouterLink } from '@angular/router';
import { CameraApiService } from '../services/camera-api.service';
import { CameraSummary } from '../models/camera-models';

@Component({
  standalone: true,
  imports: [NgFor, RouterLink],
  template: `
    <main class="dashboard">
      <h2>Camera Dashboard</h2>
      <p class="subtitle">Select a live camera feed from your relay registry.</p>
      <section class="grid">
        <article *ngFor="let c of cameras" class="card">
          <div class="badge">#{{c.id}}</div>
          <h3>{{c.name}}</h3>
          <p>mDNS: <code>{{c.host}}</code></p>
          <p>Endpoint: {{c.host}}:{{c.port}}</p>
          <a [routerLink]="['/camera', c.id]">Open live controls →</a>
        </article>
      </section>
    </main>
  `,
  styles: ['.dashboard{padding:1.25rem}.subtitle{color:#94a3b8}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:14px;margin-top:1rem}.card{background:linear-gradient(145deg,#111827,#1f2937);border:1px solid #334155;padding:1rem;border-radius:12px;box-shadow:0 12px 24px rgba(2,6,23,.35)}h3{margin:.2rem 0 .6rem}.badge{display:inline-block;background:#0ea5e9;color:#082f49;border-radius:999px;padding:.15rem .55rem;font-size:.75rem;font-weight:700}a{color:#7dd3fc;text-decoration:none;font-weight:600}a:hover{text-decoration:underline}code{color:#bfdbfe}']
})
export class DashboardComponent implements OnInit {
  cameras: CameraSummary[] = [];
  constructor(private api: CameraApiService) {}
  ngOnInit(): void { this.api.cameras().subscribe(v => this.cameras = v); }
}

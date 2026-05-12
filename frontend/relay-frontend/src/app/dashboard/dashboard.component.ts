import { Component, OnInit } from '@angular/core';
import { NgFor } from '@angular/common';
import { RouterLink } from '@angular/router';
import { CameraApiService } from '../services/camera-api.service';
import { CameraSummary } from '../models/camera-models';

@Component({
  standalone: true,
  imports: [NgFor, RouterLink],
  template: `
    <main><h1>Camera Dashboard</h1><p>Select a relayed camera.</p>
    <section class="grid">
      <article *ngFor="let c of cameras">
        <h3>{{c.name}}</h3>
        <p><b>ID:</b> {{c.id}}</p>
        <p><b>Relay target:</b> {{c.host}}:{{c.port}}</p>
        <a [routerLink]="['/camera', c.id]">Open camera</a>
      </article>
    </section></main>
  `,
  styles: ['main{padding:1rem}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px}article{background:#1b1b1b;padding:12px;border-radius:8px}a{color:#7fd2ff}']
})
export class DashboardComponent implements OnInit {
  cameras: CameraSummary[] = [];
  constructor(private api: CameraApiService) {}
  ngOnInit(): void { this.api.cameras().subscribe(v => this.cameras = v); }
}

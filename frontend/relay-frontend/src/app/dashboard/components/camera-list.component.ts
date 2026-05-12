import { Component, Input } from '@angular/core';
import { NgFor } from '@angular/common';
import { RouterLink } from '@angular/router';
import { CameraSummary } from '../../models/camera-models';

@Component({
  selector: 'app-camera-list',
  standalone: true,
  imports: [NgFor, RouterLink],
  template: `<section class="grid"><article *ngFor="let c of cameras" class="card"><h3>{{c.name}}</h3><p>ID: {{c.id}}</p><p>mDNS: <code>{{c.host}}</code>:{{c.port}}</p><p class="state">Offline / unreachable</p><a [routerLink]="['/camera', c.id]">Open controls →</a></article></section>`,
  styles: ['.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:14px}.card{background:linear-gradient(160deg,#111827,#0b1220);border:1px solid #334155;border-radius:12px;padding:1rem}.state{color:#fbbf24;font-weight:600}a{color:#7dd3fc;text-decoration:none;font-weight:700}']
})
export class CameraListComponent { @Input() cameras: CameraSummary[] = []; }

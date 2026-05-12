import { Component, Input } from '@angular/core';

@Component({
  selector: 'app-dashboard-hero',
  standalone: true,
  template: `
    <section class="hero">
      <h2>Camera Dashboard</h2>
      <p>Relay overview and camera registry.</p>
      <div class="chips">
        <span>Total cameras: {{total}}</span>
        <span>Online: {{online}}</span>
        <span>Offline: {{total - online}}</span>
      </div>
    </section>
  `,
  styles: ['.hero{padding:1rem;border:1px solid #334155;border-radius:14px;background:linear-gradient(120deg,#0f172a,#1e293b)}h2{margin:0 0 .35rem}.chips{display:flex;gap:.5rem;flex-wrap:wrap;margin-top:.5rem}span{background:#0b1220;border:1px solid #334155;border-radius:999px;padding:.2rem .6rem;font-size:.8rem}']
})
export class DashboardHeroComponent { @Input() total = 0; @Input() online = 0; }

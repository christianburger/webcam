import { Component } from '@angular/core';
import { RouterLink, RouterOutlet } from '@angular/router';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [RouterOutlet, RouterLink],
  template: `
    <header><a routerLink="/dashboard">#dashboard</a></header>
    <router-outlet />
  `,
  styles: [`header{padding:12px;background:#121212}a{color:#7fd2ff;text-decoration:none;font-weight:700}`]
})
export class AppComponent {}

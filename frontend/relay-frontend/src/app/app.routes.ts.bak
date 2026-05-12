import { Routes } from '@angular/router';
import { DashboardComponent } from './dashboard/dashboard.component';
import { CameraComponent } from './camera/camera.component';

export const routes: Routes = [
  { path: '', redirectTo: 'dashboard', pathMatch: 'full' },
  { path: 'dashboard', component: DashboardComponent },
  { path: 'camera/:id', component: CameraComponent },
  { path: '**', redirectTo: 'dashboard' }
];

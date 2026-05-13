import { Routes } from '@angular/router';
import { DashboardComponent } from './dashboard/dashboard.component';
import { CameraComponent } from './camera/camera.component';

export const routes: Routes = [
    { path: '', pathMatch: 'full', redirectTo: 'dashboard' },
    { path: 'dashboard', component: DashboardComponent },
    { path: 'camera/:id', component: CameraComponent },
    { path: '**', redirectTo: 'dashboard' }
];
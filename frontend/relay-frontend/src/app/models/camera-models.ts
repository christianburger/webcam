export interface CameraSummary {
  id: string;
  name: string;
  host: string;
  port: number;
}

export interface CameraStatus {
  cameraId: string;
  reachable: boolean;
  statusCode: number;
  body: string;
  observedAt: string;
}

export interface SessionState {
  cameraId: string;
  active: boolean;
  changedAt: string;
}

export interface FramePayload {
  cameraId: string;
  contentType: string;
  data: number[];
  observedAt: string;
}

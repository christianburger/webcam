package com.runtracer.relaybackend.camera;

import java.time.Instant;

public class CameraModels {
    public record CameraSummary(String id, String name, String host, int port, String uri) {}
    public record CameraStatus(String cameraId, boolean reachable, int statusCode, String body, Instant observedAt) {}
    public record SessionState(String cameraId, boolean active, Instant changedAt) {}
    public record FramePayload(String cameraId, String contentType, byte[] data, Instant observedAt) {}
}
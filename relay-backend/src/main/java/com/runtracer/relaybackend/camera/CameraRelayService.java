package com.runtracer.relaybackend.camera;

import com.runtracer.relaybackend.config.AppProperties;
import java.net.URI;
import java.time.Instant;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestClient;

@Service
public class CameraRelayService {
    private final AppProperties props;
    private final RestClient restClient;
    private final Map<String, Boolean> sessions = new ConcurrentHashMap<>();

    public CameraRelayService(AppProperties props, RestClient restClient) {
        this.props = props;
        this.restClient = restClient;
    }

    public List<CameraModels.CameraSummary> cameras() {
        return props.getCamera().getRegistry().stream()
            .map(c -> new CameraModels.CameraSummary(c.getId(), c.getName(), c.getHost(), c.getPort()))
            .toList();
    }

    public CameraModels.CameraStatus status(String id) {
        var cam = getCamera(id);
        try {
            var response = restClient.get().uri(uri(cam, "/status")).retrieve().toEntity(String.class);
            return new CameraModels.CameraStatus(id, true, response.getStatusCode().value(), response.getBody(), Instant.now());
        } catch (Exception ex) {
            return new CameraModels.CameraStatus(id, false, 503, ex.getMessage(), Instant.now());
        }
    }

    public CameraModels.SessionState startSession(String id) { sessions.put(id, true); return new CameraModels.SessionState(id, true, Instant.now()); }
    public CameraModels.SessionState stopSession(String id) { sessions.put(id, false); return new CameraModels.SessionState(id, false, Instant.now()); }

    public Map<String, Object> frameLatest(String id) {
        var cam = getCamera(id);
        try {
            var bytes = restClient.get().uri(uri(cam, "/capture")).retrieve().body(byte[].class);
            return Map.of("cameraId", id, "contentType", "image/jpeg", "capturedAt", Instant.now().toString(),
                "base64", Base64.getEncoder().encodeToString(bytes == null ? new byte[0] : bytes));
        } catch (Exception ex) {
            return Map.of("cameraId", id, "error", ex.getMessage(), "capturedAt", Instant.now().toString());
        }
    }

    public Map<String, Object> periphState(String id) {
        var cam = getCamera(id);
        try {
            var body = restClient.get().uri(uri(cam, "/periph/state")).retrieve().body(String.class);
            return Map.of("cameraId", id, "observedAt", Instant.now().toString(), "body", body == null ? "{}" : body);
        } catch (Exception ex) {
            return Map.of("cameraId", id, "observedAt", Instant.now().toString(), "error", ex.getMessage());
        }
    }

    public Map<String, Object> control(String id, String name, Integer angle, Integer state) {
        var cam = getCamera(id);
        try {
            String query = angle != null ? "angle=" + angle : "state=" + (state == null ? 0 : state);
            var body = restClient.get().uri(uri(cam, "/control/" + name + "?" + query)).retrieve().body(String.class);
            return Map.of("cameraId", id, "control", name, "ok", true, "body", body == null ? "ok" : body, "observedAt", Instant.now().toString());
        } catch (Exception ex) {
            return Map.of("cameraId", id, "control", name, "ok", false, "error", ex.getMessage(), "observedAt", Instant.now().toString());
        }
    }

    private AppProperties.CameraEntry getCamera(String id) {
        return props.getCamera().getRegistry().stream().filter(c -> c.getId().equals(id)).findFirst()
            .orElseThrow(() -> new IllegalArgumentException("Unknown camera id: " + id));
    }

    private URI uri(AppProperties.CameraEntry c, String path) {
        return URI.create("http://" + c.getHost() + ":" + c.getPort() + path);
    }
}

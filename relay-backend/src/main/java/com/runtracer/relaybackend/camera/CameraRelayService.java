package com.runtracer.relaybackend.camera;

import com.runtracer.relaybackend.config.AppProperties;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URI;
import java.time.Instant;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestClient;

@Service
public class CameraRelayService {

    private static final Logger log = LoggerFactory.getLogger(CameraRelayService.class);

    private final AppProperties props;
    private final RestClient restClient;
    private final Map<String, Boolean> sessions = new ConcurrentHashMap<>();

    public CameraRelayService(AppProperties props, RestClient restClient) {
        this.props = props;
        this.restClient = restClient;
        log.info("[RELAY] initialized with {} camera(s)", props.getCamera().getRegistry().size());
        props.getCamera().getRegistry().forEach(c ->
                log.info("[RELAY]   camera: id='{}' name='{}' host='{}' port={}",
                        c.getId(), c.getName(), c.getHost(), c.getPort()));
    }

    public List<CameraModels.CameraSummary> cameras() {
        var list = props.getCamera().getRegistry().stream()
                .map(c -> new CameraModels.CameraSummary(
                        c.getId(), c.getName(), c.getHost(), c.getPort(),
                        "http://" + c.getHost() + ":" + c.getPort() + "/"))
                .toList();
        log.debug("[RELAY] cameras() → {} entries", list.size());
        return list;
    }

    public CameraModels.CameraStatus status(String id) {
        var cam = getCamera(id);
        URI target = uri(cam, "/status");
        log.info("[RELAY] status({}) → GET {}", id, target);
        long t0 = System.currentTimeMillis();
        try {
            var resp = restClient.get().uri(target).retrieve().toEntity(String.class);
            long ms = System.currentTimeMillis() - t0;
            log.info("[RELAY] status({}) ← HTTP {} in {}ms  body={}",
                    id, resp.getStatusCode().value(), ms, resp.getBody());
            return new CameraModels.CameraStatus(
                    id, true, resp.getStatusCode().value(), resp.getBody(), Instant.now());
        } catch (Exception ex) {
            long ms = System.currentTimeMillis() - t0;
            log.error("[RELAY] status({}) ← ERROR in {}ms: {} — {}",
                    id, ms, ex.getClass().getSimpleName(), ex.getMessage());
            return new CameraModels.CameraStatus(id, false, 503, ex.getMessage(), Instant.now());
        }
    }

    public Map<String, Object> periphState(String id) {
        var cam = getCamera(id);
        URI target = uri(cam, "/periph/state");
        log.info("[RELAY] periphState({}) → GET {}", id, target);
        long t0 = System.currentTimeMillis();
        try {
            String body = restClient.get().uri(target).retrieve().body(String.class);
            long ms = System.currentTimeMillis() - t0;
            log.info("[RELAY] periphState({}) ← in {}ms  body={}", id, ms, body);
            return Map.of(
                    "cameraId", id,
                    "observedAt", Instant.now().toString(),
                    "body", body == null ? "{}" : body);
        } catch (Exception ex) {
            long ms = System.currentTimeMillis() - t0;
            log.error("[RELAY] periphState({}) ← ERROR in {}ms: {} — {}",
                    id, ms, ex.getClass().getSimpleName(), ex.getMessage());
            return Map.of(
                    "cameraId", id,
                    "observedAt", Instant.now().toString(),
                    "error", ex.getMessage());
        }
    }

    public Map<String, Object> control(String id, String name, Integer angle, Integer state) {
        var cam = getCamera(id);
        String query = angle != null ? "angle=" + angle : "state=" + (state == null ? 0 : state);
        URI target = uri(cam, "/control/" + name + "?" + query);
        log.info("[RELAY] control({}, {}) → GET {}", id, name, target);
        long t0 = System.currentTimeMillis();
        try {
            String body = restClient.get().uri(target).retrieve().body(String.class);
            long ms = System.currentTimeMillis() - t0;
            log.info("[RELAY] control({}, {}) ← in {}ms  body={}", id, name, ms, body);
            return Map.of(
                    "cameraId", id, "control", name, "ok", true,
                    "body", body == null ? "ok" : body,
                    "observedAt", Instant.now().toString());
        } catch (Exception ex) {
            long ms = System.currentTimeMillis() - t0;
            log.error("[RELAY] control({}, {}) ← ERROR in {}ms: {} — {}",
                    id, name, ms, ex.getClass().getSimpleName(), ex.getMessage());
            return Map.of(
                    "cameraId", id, "control", name, "ok", false,
                    "error", ex.getMessage(),
                    "observedAt", Instant.now().toString());
        }
    }

    // ── Streaming proxies ─────────────────────────────────────────────────────

    /**
     * Proxies the MJPEG stream from the ESP32 to the given OutputStream.
     * Blocks until the client disconnects or the camera goes offline.
     */
    public void proxyStream(String id, OutputStream out) throws IOException {
        var cam = getCamera(id);
        String urlStr = "http://" + cam.getHost() + ":" + cam.getPort() + "/stream";
        log.info("[RELAY] proxyStream({}) → {}", id, urlStr);
        HttpURLConnection conn = (HttpURLConnection) URI.create(urlStr).toURL().openConnection();
        conn.setConnectTimeout(5_000);
        conn.setReadTimeout(0);        // no read timeout — stream is indefinite
        try (InputStream in = conn.getInputStream()) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) != -1) {
                out.write(buf, 0, n);
                out.flush();
            }
        } finally {
            conn.disconnect();
            log.info("[RELAY] proxyStream({}) ended", id);
        }
    }

    /**
     * Proxies a single JPEG capture from the ESP32 to the given OutputStream.
     */
    public void proxyCapture(String id, OutputStream out) throws IOException {
        var cam = getCamera(id);
        String urlStr = "http://" + cam.getHost() + ":" + cam.getPort() + "/capture";
        log.info("[RELAY] proxyCapture({}) → {}", id, urlStr);
        HttpURLConnection conn = (HttpURLConnection) URI.create(urlStr).toURL().openConnection();
        conn.setConnectTimeout(5_000);
        conn.setReadTimeout(10_000);
        try (InputStream in = conn.getInputStream()) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) != -1) {
                out.write(buf, 0, n);
            }
        } finally {
            conn.disconnect();
            log.info("[RELAY] proxyCapture({}) done", id);
        }
    }

    // ── Retained for API backwards compat (frame/latest) ─────────────────────

    public Map<String, Object> frameLatest(String id) {
        var cam = getCamera(id);
        URI target = uri(cam, "/capture");
        log.info("[RELAY] frameLatest({}) → GET {}", id, target);
        long t0 = System.currentTimeMillis();
        try {
            byte[] bytes = restClient.get().uri(target).retrieve().body(byte[].class);
            long ms = System.currentTimeMillis() - t0;
            int size = bytes == null ? 0 : bytes.length;
            log.info("[RELAY] frameLatest({}) ← {} bytes in {}ms", id, size, ms);
            return Map.of(
                    "cameraId", id,
                    "contentType", "image/jpeg",
                    "capturedAt", Instant.now().toString(),
                    "base64", Base64.getEncoder().encodeToString(bytes == null ? new byte[0] : bytes));
        } catch (Exception ex) {
            long ms = System.currentTimeMillis() - t0;
            log.error("[RELAY] frameLatest({}) ← ERROR in {}ms: {} — {}",
                    id, ms, ex.getClass().getSimpleName(), ex.getMessage());
            return Map.of(
                    "cameraId", id,
                    "error", ex.getMessage(),
                    "capturedAt", Instant.now().toString());
        }
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private AppProperties.CameraEntry getCamera(String id) {
        return props.getCamera().getRegistry().stream()
                .filter(c -> c.getId().equals(id))
                .findFirst()
                .orElseThrow(() -> {
                    var known = props.getCamera().getRegistry().stream()
                            .map(AppProperties.CameraEntry::getId).toList();
                    log.error("[RELAY] camera '{}' NOT FOUND. Known IDs: {}", id, known);
                    return new IllegalArgumentException("Unknown camera id: " + id);
                });
    }

    private URI uri(AppProperties.CameraEntry c, String path) {
        return URI.create("http://" + c.getHost() + ":" + c.getPort() + path);
    }
}

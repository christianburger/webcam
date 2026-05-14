package com.runtracer.relaybackend.web;

import com.runtracer.relaybackend.camera.CameraRelayService;
import jakarta.servlet.http.HttpServletResponse;
import java.io.IOException;
import java.time.Instant;
import java.util.Map;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.web.bind.annotation.*;

@RestController
@RequestMapping("/api")
public class RelayApiController {

    private static final Logger log = LoggerFactory.getLogger(RelayApiController.class);
    private final CameraRelayService relay;

    public RelayApiController(CameraRelayService relay) {
        this.relay = relay;
    }

    @GetMapping("/health")
    public Map<String, Object> health() {
        log.info("[API] GET /api/health");
        return Map.of("status", "ok", "observedAt", Instant.now().toString());
    }

    @GetMapping("/cameras")
    public Object cameras() {
        log.info("[API] GET /api/cameras");
        var result = relay.cameras();
        log.info("[API] /cameras → {} camera(s)", result.size());
        return result;
    }

    @GetMapping("/cameras/{id}/status")
    public Object status(@PathVariable String id) {
        log.info("[API] GET /api/cameras/{}/status", id);
        return relay.status(id);
    }

    // ── MJPEG stream proxy ────────────────────────────────────────────────────
    // The browser <img src="..."> connects here; Spring holds the thread and
    // pipes bytes from the ESP32 /stream endpoint until the client disconnects.
    @GetMapping("/cameras/{id}/stream")
    public void stream(@PathVariable String id, HttpServletResponse response) {
        log.info("[API] GET /api/cameras/{}/stream", id);
        response.setContentType("multipart/x-mixed-replace;boundary=ESP32CAMBOUNDARY");
        response.setHeader("Cache-Control", "no-cache, no-store");
        response.setHeader("Access-Control-Allow-Origin", "*");
        try {
            relay.proxyStream(id, response.getOutputStream());
        } catch (IOException e) {
            // Normal when the browser tab is closed or user navigates away
            log.info("[API] stream({}) client disconnected: {}", id, e.getMessage());
        }
    }

    // ── Direct JPEG capture ───────────────────────────────────────────────────
    // Opens inline in the browser or can be saved as a file.
    @GetMapping("/cameras/{id}/capture")
    public void capture(@PathVariable String id, HttpServletResponse response) {
        log.info("[API] GET /api/cameras/{}/capture", id);
        response.setContentType("image/jpeg");
        response.setHeader("Content-Disposition", "inline; filename=capture.jpg");
        response.setHeader("Cache-Control", "no-store");
        response.setHeader("Access-Control-Allow-Origin", "*");
        try {
            relay.proxyCapture(id, response.getOutputStream());
        } catch (IOException e) {
            log.error("[API] capture({}) error: {}", id, e.getMessage());
            response.setStatus(503);
        }
    }

    // ── Retained for backwards-compat (base64 JSON) ───────────────────────────
    @GetMapping("/cameras/{id}/frame/latest")
    public Object frame(@PathVariable String id) {
        log.info("[API] GET /api/cameras/{}/frame/latest", id);
        return relay.frameLatest(id);
    }

    @GetMapping("/cameras/{id}/periph/state")
    public Object periph(@PathVariable String id) {
        log.info("[API] GET /api/cameras/{}/periph/state", id);
        return relay.periphState(id);
    }

    @GetMapping("/cameras/{id}/control/{name}")
    public Object control(
            @PathVariable String id,
            @PathVariable String name,
            @RequestParam(required = false) Integer angle,
            @RequestParam(required = false) Integer state) {
        log.info("[API] GET /api/cameras/{}/control/{} angle={} state={}", id, name, angle, state);
        var result = relay.control(id, name, angle, state);
        log.info("[API] /cameras/{}/control/{} → ok={}", id, name, result.get("ok"));
        return result;
    }
}

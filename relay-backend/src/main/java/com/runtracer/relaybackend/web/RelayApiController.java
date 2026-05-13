package com.runtracer.relaybackend.web;

import com.runtracer.relaybackend.camera.CameraRelayService;
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
        log.info("[API] /cameras → returning {} camera(s): {}",
                result.size(),
                result.stream().map(c -> c.id() + "@" + c.host()).toList());
        return result;
    }

    @GetMapping("/cameras/{id}/status")
    public Object status(@PathVariable String id) {
        log.info("[API] GET /api/cameras/{}/status", id);
        var result = relay.status(id);
        log.info("[API] /cameras/{}/status → reachable={} httpStatus={}",
                id, result.reachable(), result.statusCode());
        return result;
    }

    @PostMapping("/cameras/{id}/session/start")
    public Object start(@PathVariable String id) {
        log.info("[API] POST /api/cameras/{}/session/start", id);
        var result = relay.startSession(id);
        log.info("[API] /cameras/{}/session/start → active={}", id, result.active());
        return result;
    }

    @PostMapping("/cameras/{id}/session/stop")
    public Object stop(@PathVariable String id) {
        log.info("[API] POST /api/cameras/{}/session/stop", id);
        var result = relay.stopSession(id);
        log.info("[API] /cameras/{}/session/stop → active={}", id, result.active());
        return result;
    }

    @GetMapping("/cameras/{id}/frame/latest")
    public Object frame(@PathVariable String id) {
        log.info("[API] GET /api/cameras/{}/frame/latest", id);
        var result = relay.frameLatest(id);
        boolean hasFrame = result.containsKey("base64");
        log.info("[API] /cameras/{}/frame/latest → hasBase64={} error={}",
                id, hasFrame, result.getOrDefault("error", "none"));
        return result;
    }

    @GetMapping("/cameras/{id}/periph/state")
    public Object periph(@PathVariable String id) {
        log.info("[API] GET /api/cameras/{}/periph/state", id);
        var result = relay.periphState(id);
        log.info("[API] /cameras/{}/periph/state → {}", id, result);
        return result;
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
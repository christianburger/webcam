package com.runtracer.relaybackend.web;

import com.runtracer.relaybackend.camera.CameraRelayService;
import java.time.Instant;
import java.util.Map;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

@RestController
@RequestMapping("/api")
public class RelayApiController {

    private final CameraRelayService relay;

    public RelayApiController(CameraRelayService relay) {
        this.relay = relay;
    }

    @GetMapping("/health")
    public Map<String, Object> health() {
        return Map.of("status", "ok", "observedAt", Instant.now().toString());
    }

    @GetMapping("/cameras")
    public Object cameras() { return relay.cameras(); }

    @GetMapping("/cameras/{id}/status")
    public Object status(@PathVariable String id) { return relay.status(id); }

    @PostMapping("/cameras/{id}/session/start")
    public Object start(@PathVariable String id) { return relay.startSession(id); }

    @PostMapping("/cameras/{id}/session/stop")
    public Object stop(@PathVariable String id) { return relay.stopSession(id); }

    @GetMapping("/cameras/{id}/frame/latest")
    public Object frame(@PathVariable String id) { return relay.frameLatest(id); }

    @GetMapping("/cameras/{id}/periph/state")
    public Object periph(@PathVariable String id) { return relay.periphState(id); }

    @GetMapping("/cameras/{id}/control/{name}")
    public Object control(@PathVariable String id, @PathVariable String name,
                          @RequestParam(required = false) Integer angle,
                          @RequestParam(required = false) Integer state) {
        return relay.control(id, name, angle, state);
    }
}

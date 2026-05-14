package com.runtracer.relaybackend.config;

import jakarta.validation.constraints.Min;
import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.NotEmpty;
import java.util.ArrayList;
import java.util.List;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.validation.annotation.Validated;

@Validated
@ConfigurationProperties(prefix = "app")
public class AppProperties {

    @NotBlank
    private String publicBaseUrl;

    private CameraProperties camera = new CameraProperties();

    public String getPublicBaseUrl() {
        return publicBaseUrl;
    }

    public void setPublicBaseUrl(String publicBaseUrl) {
        this.publicBaseUrl = publicBaseUrl;
    }

    public CameraProperties getCamera() {
        return camera;
    }

    public void setCamera(CameraProperties camera) {
        this.camera = camera;
    }

    public static class CameraProperties {
        @Min(1)
        private long resolveTtlSeconds = 60;
        @Min(200)
        private int defaultTimeoutMs = 3000;
        @NotEmpty
        private List<CameraEntry> registry = new ArrayList<>();

        public long getResolveTtlSeconds() {
            return resolveTtlSeconds;
        }

        public void setResolveTtlSeconds(long resolveTtlSeconds) {
            this.resolveTtlSeconds = resolveTtlSeconds;
        }

        public int getDefaultTimeoutMs() {
            return defaultTimeoutMs;
        }

        public void setDefaultTimeoutMs(int defaultTimeoutMs) {
            this.defaultTimeoutMs = defaultTimeoutMs;
        }

        public List<CameraEntry> getRegistry() {
            return registry;
        }

        public void setRegistry(List<CameraEntry> registry) {
            this.registry = registry;
        }
    }

    public static class CameraEntry {
        @NotBlank
        private String id;
        @NotBlank
        private String name;
        @NotBlank
        private String host;
        @Min(1)
        private int port = 80;

        public String getId() { return id; }
        public void setId(String id) { this.id = id; }
        public String getName() { return name; }
        public void setName(String name) { this.name = name; }
        public String getHost() { return host; }
        public void setHost(String host) { this.host = host; }
        public int getPort() { return port; }
        public void setPort(int port) { this.port = port; }
    }
}

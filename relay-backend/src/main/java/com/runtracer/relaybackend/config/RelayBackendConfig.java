package com.runtracer.relaybackend.config;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.http.client.SimpleClientHttpRequestFactory;
import org.springframework.web.client.RestClient;
import org.springframework.web.servlet.config.annotation.CorsRegistry;
import org.springframework.web.servlet.config.annotation.WebMvcConfigurer;

@Configuration
@EnableConfigurationProperties(AppProperties.class)
public class RelayBackendConfig {

    private static final Logger log = LoggerFactory.getLogger(RelayBackendConfig.class);

    @Bean
    RestClient restClient(AppProperties props) {
        int timeoutMs = props.getCamera().getDefaultTimeoutMs();
        log.info("[CONFIG] RestClient timeout={}ms (connect+read)", timeoutMs);

        SimpleClientHttpRequestFactory factory = new SimpleClientHttpRequestFactory();
        factory.setConnectTimeout(timeoutMs);
        factory.setReadTimeout(timeoutMs);

        return RestClient.builder()
                .requestFactory(factory)
                .build();
    }

    /**
     * Allow the Angular dev server (localhost:4200) to call /api/** during
     * development. In production Angular is served from the same origin
     * (Spring Boot on :8080), so CORS isn't needed there.
     */
    @Bean
    WebMvcConfigurer corsConfigurer() {
        return new WebMvcConfigurer() {
            @Override
            public void addCorsMappings(CorsRegistry registry) {
                log.info("[CONFIG] CORS: /api/** allowed from localhost:4200 and :8080");
                registry.addMapping("/api/**")
                        .allowedOrigins(
                                "http://localhost:4200",
                                "http://localhost:8080")
                        .allowedMethods("GET", "POST", "PUT", "DELETE", "OPTIONS")
                        .allowedHeaders("*")
                        .maxAge(3600);
            }
        };
    }
}
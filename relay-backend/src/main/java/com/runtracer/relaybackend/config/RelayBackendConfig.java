package com.runtracer.relaybackend.config;

import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.client.RestClient;

@Configuration
@EnableConfigurationProperties(AppProperties.class)
public class RelayBackendConfig {

    @Bean
    RestClient restClient() {
        return RestClient.builder().build();
    }
}

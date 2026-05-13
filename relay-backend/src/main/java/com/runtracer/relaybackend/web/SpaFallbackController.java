package com.runtracer.relaybackend.web;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Controller;
import org.springframework.web.bind.annotation.RequestMapping;

/**
 * Forwards Angular client-side routes to index.html so that direct navigation
 * (e.g. http://localhost:8080/camera/cam-1) works correctly.
 * /api/** is handled by RelayApiController and must NOT be forwarded here.
 */
@Controller
public class SpaFallbackController {

    private static final Logger log = LoggerFactory.getLogger(SpaFallbackController.class);

    @RequestMapping(value = {"/", "/dashboard", "/camera/**"})
    public String forwardToSpa() {
        log.debug("[SPA] forwarding Angular route → /index.html");
        return "forward:/index.html";
    }
}
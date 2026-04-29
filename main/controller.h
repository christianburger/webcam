#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "esp_http_server.h"
#include "esp_camera.h"

// ─── Register all URI handlers with an already-started server ────────────────
// Call this once from network_manager after httpd_start() succeeds.
void controller_register_handlers(httpd_handle_t server);

// ─── Camera handlers ─────────────────────────────────────────────────────────
esp_err_t root_handler         (httpd_req_t *req);
esp_err_t capture_handler      (httpd_req_t *req);
esp_err_t stream_handler       (httpd_req_t *req);
esp_err_t status_handler       (httpd_req_t *req);
esp_err_t hardware_info_handler(httpd_req_t *req);

// ─── Control handlers ────────────────────────────────────────────────────────
// GET /control/pan?angle=<0-180>
esp_err_t pan_handler          (httpd_req_t *req);
// GET /control/tilt?angle=<0-180>
esp_err_t tilt_handler         (httpd_req_t *req);
// GET /control/led?state=<0|1>
esp_err_t led_handler          (httpd_req_t *req);
// GET /control/switch?state=<0|1>
esp_err_t switch_handler       (httpd_req_t *req);
// GET /periph/state  → JSON with current state of all peripherals
esp_err_t periph_state_handler (httpd_req_t *req);

#endif // CONTROLLER_H

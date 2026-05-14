#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "esp_http_server.h"

// HTTP request handlers
esp_err_t root_handler(httpd_req_t *req);
esp_err_t status_handler(httpd_req_t *req);
esp_err_t hardware_info_handler(httpd_req_t *req);
esp_err_t capture_handler(httpd_req_t *req);
esp_err_t stream_handler(httpd_req_t *req);
esp_err_t pan_handler(httpd_req_t *req);
esp_err_t tilt_handler(httpd_req_t *req);
esp_err_t led_handler(httpd_req_t *req);
esp_err_t switch_handler(httpd_req_t *req);
esp_err_t periph_state_handler(httpd_req_t *req);

// Register all routes with the HTTP server
void controller_register_handlers(httpd_handle_t server);

#endif /* CONTROLLER_H */
#include "network_manager.h"
#include "wifi_manager.h"
#include "controller.h"
#include "peripherals.h"
#include "cam_log.h"

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "mdns.h"

static httpd_handle_t s_server = NULL;

// ─── mDNS ────────────────────────────────────────────────────────────────────

static void start_mdns(void) {
    mdns_init();
    mdns_hostname_set("web-cam");
    mdns_instance_name_set("ESP32 Web Cam");
    ESP_LOGI(TG_NET_HTTP, "mDNS: web-cam.local");
}

// ─── HTTP server ──────────────────────────────────────────────────────────────

static void start_webserver(void) {
    if (s_server) return;   // already running

    httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size        = NETWORK_TASK_STACK_SIZE;
    cfg.task_priority     = NETWORK_TASK_PRIORITY;
    cfg.core_id           = NETWORK_TASK_CORE_ID;
    cfg.max_uri_handlers  = 12;
    cfg.max_open_sockets  = 4;
    cfg.lru_purge_enable  = true;
    cfg.uri_match_fn      = httpd_uri_match_wildcard;

    ESP_LOGI(TG_NET_HTTP, "HTTP parser limits: hdr=%d uri=%d",
             CONFIG_HTTPD_MAX_REQ_HDR_LEN, CONFIG_HTTPD_MAX_URI_LEN);

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TG_NET_HTTP, "httpd_start() failed");
        return;
    }
    controller_register_handlers(s_server);
    ESP_LOGI(TG_NET_HTTP, "HTTP server ready on port 80");
}

// ─── network_task ─────────────────────────────────────────────────────────────

void network_task(void *pvParameters) {
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    // NVS must be ready before wifi_manager_init (driver needs it for cal data)
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    ESP_ERROR_CHECK(peripherals_init());
    ESP_ERROR_CHECK(wifi_manager_init());   // starts WiFi stack + manager task

    EventGroupHandle_t wm_evt = wifi_manager_get_event_group();
    bool mdns_started  = false;
    bool http_started  = false;

    while (1) {
        esp_task_wdt_reset();

        EventBits_t bits = xEventGroupGetBits(wm_evt);

        if (bits & WM_CONNECTED_BIT) {
            if (!mdns_started) { start_mdns();      mdns_started = true; }
            if (!http_started) { start_webserver(); http_started = true; }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

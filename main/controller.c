#include "controller.h"
#include "peripherals.h"
#include "wifi_manager.h"
#include "cam_log.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "img_converters.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <lwip/inet.h>

extern QueueHandle_t frame_queue;

#define SW_JPEG_QUALITY  80
#define STREAM_BOUNDARY  "ESP32CAMBOUNDARY"

// ─── Request / response logging ───────────────────────────────────────────────

static void log_request(httpd_req_t *req, const char *tag) {
    int sockfd = httpd_req_to_sockfd(req);
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    char client_ip[INET_ADDRSTRLEN] = "?";
    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_len) == 0)
        inet_ntop(AF_INET, &addr.sin_addr, client_ip, sizeof(client_ip));
    ESP_LOGI(tag, ">>> %s %s  client=%s  heap=%lu B",
             req->method == HTTP_GET ? "GET" : "POST",
             req->uri, client_ip,
             (unsigned long)esp_get_free_heap_size());
}

static void log_response(const char *tag, bool ok, size_t bytes, int64_t t0_us) {
    int64_t elapsed = esp_timer_get_time() - t0_us;
    ESP_LOGI(tag, "<<< %s  %zu B  %lld µs",
             ok ? "200 OK" : "ERROR", bytes, (long long)elapsed);
}

// ─── Shared helpers ───────────────────────────────────────────────────────────

static esp_err_t query_int(httpd_req_t *req, const char *key, int *out) {
    char qs[64];
    if (httpd_req_get_url_query_str(req, qs, sizeof(qs)) != ESP_OK) return ESP_FAIL;
    char val[16];
    if (httpd_query_key_value(qs, key, val, sizeof(val)) != ESP_OK) return ESP_FAIL;
    *out = atoi(val);
    return ESP_OK;
}

static void send_ok(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"ok\":true}");
}

static void send_err(httpd_req_t *req, const char *msg) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_status(req, "400 Bad Request");
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", msg);
    httpd_resp_sendstr(req, buf);
}

static bool get_jpeg(camera_fb_t *pic, uint8_t **out_buf, size_t *out_len,
                     bool *converted) {
    *converted = false;
    if (pic->format == PIXFORMAT_JPEG) {
        *out_buf = pic->buf;
        *out_len = pic->len;
        return true;
    }
    if (!frame2jpg(pic, SW_JPEG_QUALITY, out_buf, out_len)) {
        ESP_LOGE(TG_CTL_CAPT, "SW JPEG encode failed (fmt=%d)", pic->format);
        return false;
    }
    *converted = true;
    return true;
}

// ─── Handlers ─────────────────────────────────────────────────────────────────

esp_err_t root_handler(httpd_req_t *req) {
    int64_t t0 = esp_timer_get_time();
    log_request(req, TG_NET_HTTP);
    const char *body =
        "{"
        "\"service\":\"ESP32-CAM\","
        "\"endpoints\":["
            "\"/status\",\"/capture\",\"/stream\",\"/hardware\","
            "\"/periph/state\",\"/control/pan\",\"/control/tilt\","
            "\"/control/led\",\"/control/switch\""
        "]"
        "}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, body);
    log_response(TG_NET_HTTP, true, strlen(body), t0);
    return ESP_OK;
}

// ─── /status — system + WiFi health ──────────────────────────────────────────
//
//  Response schema:
//  {
//    "heap":      <bytes>,
//    "tasks":     <count>,
//    "cpu_mhz":   <mhz>,
//    "wifi": {
//      "state":      "CONNECTED" | "DEGRADED" | "SCANNING" | ...,
//      "ssid":       "<ssid>",
//      "rssi_dbm":   <int8>,
//      "ip":         "<a.b.c.d>",
//      "uptime_s":   <uint32>,
//      "degraded":   true|false,
//      "switches":   <uint32>,
//      "reconnects": <uint32>
//    }
//  }

esp_err_t status_handler(httpd_req_t *req) {
    int64_t t0 = esp_timer_get_time();
    log_request(req, TG_NET_HTTP);

    wifi_manager_info_t wi;
    wifi_manager_get_info(&wi);

    char buf[512];
    int n = snprintf(buf, sizeof(buf),
        "{"
        "\"heap\":%lu,"
        "\"tasks\":%d,"
        "\"cpu_mhz\":%d,"
        "\"wifi\":{"
          "\"state\":\"%s\","
          "\"ssid\":\"%s\","
          "\"rssi_dbm\":%d,"
          "\"ip\":\"%s\","
          "\"uptime_s\":%lu,"
          "\"degraded\":%s,"
          "\"switches\":%lu,"
          "\"reconnects\":%lu"
        "}"
        "}",
        (unsigned long)esp_get_free_heap_size(),
        uxTaskGetNumberOfTasks(),
        CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        wifi_manager_state_str(wi.state),
        wi.ssid,
        (int)wi.rssi_dbm,
        wi.ip,
        (unsigned long)wi.uptime_s,
        wi.degraded ? "true" : "false",
        (unsigned long)wi.switches,
        (unsigned long)wi.reconnects);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, buf, n);
    log_response(TG_NET_HTTP, true, (size_t)n, t0);
    return ESP_OK;
}

esp_err_t hardware_info_handler(httpd_req_t *req) {
    int64_t t0 = esp_timer_get_time();
    log_request(req, TG_NET_HTTP);
    char buf[512];
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    int n = snprintf(buf, sizeof(buf),
        "{"
        "\"model\":\"ESP32\","
        "\"cores\":%d,"
        "\"revision\":%d,"
        "\"psram_size\":%d,"
        "\"features\":{"
          "\"wifi\":%s,\"bt\":%s,\"ble\":%s,\"embedded_psram\":%s"
        "}"
        "}",
        ci.cores, ci.revision, esp_psram_get_size(),
        (ci.features & CHIP_FEATURE_WIFI_BGN) ? "true" : "false",
        (ci.features & CHIP_FEATURE_BT)        ? "true" : "false",
        (ci.features & CHIP_FEATURE_BLE)       ? "true" : "false",
        (ci.features & CHIP_FEATURE_EMB_PSRAM) ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, buf, n);
    log_response(TG_NET_HTTP, true, (size_t)n, t0);
    return ESP_OK;
}

esp_err_t capture_handler(httpd_req_t *req) {
    int64_t t0 = esp_timer_get_time();
    log_request(req, TG_CTL_CAPT);

    camera_fb_t *stale;
    int drained = 0;
    while (xQueueReceive(frame_queue, &stale, 0) == pdTRUE && stale) {
        esp_camera_fb_return(stale);
        drained++;
    }
    if (drained) ESP_LOGD(TG_CTL_CAPT, "drained %d stale frame(s)", drained);

    camera_fb_t *pic = esp_camera_fb_get();
    if (!pic) {
        ESP_LOGE(TG_CTL_CAPT, "fb_get returned NULL");
        httpd_resp_send_500(req);
        log_response(TG_CTL_CAPT, false, 0, t0);
        return ESP_FAIL;
    }
    ESP_LOGI(TG_CTL_CAPT, "frame %zu B  in %lld µs",
             pic->len, (long long)(esp_timer_get_time() - t0));

    uint8_t *jpg = NULL;
    size_t   len = 0;
    bool converted = false;
    if (!get_jpeg(pic, &jpg, &len, &converted)) {
        esp_camera_fb_return(pic);
        httpd_resp_send_500(req);
        log_response(TG_CTL_CAPT, false, 0, t0);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t res = httpd_resp_send(req, (const char *)jpg, (ssize_t)len);
    if (converted) free(jpg);
    esp_camera_fb_return(pic);
    log_response(TG_CTL_CAPT, res == ESP_OK, len, t0);
    return res;
}

esp_err_t stream_handler(httpd_req_t *req) {
    int64_t t0 = esp_timer_get_time();
    log_request(req, TG_CTL_STRM);
    ESP_LOGI(TG_CTL_STRM, "stream client connected  task=%s  core=%d",
             pcTaskGetName(NULL), xPortGetCoreID());

    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    uint32_t frame_idx = 0;
    while (1) {
        camera_fb_t *pic;
        if (xQueueReceive(frame_queue, &pic, pdMS_TO_TICKS(2000)) != pdTRUE || !pic) {
            ESP_LOGW(TG_CTL_STRM, "frame timeout");
            break;
        }

        uint8_t *jpg = NULL;
        size_t   len = 0;
        bool converted = false;
        if (!get_jpeg(pic, &jpg, &len, &converted)) {
            esp_camera_fb_return(pic);
            break;
        }

        char hdr[160];
        int hdr_len = snprintf(hdr, sizeof(hdr),
            "--" STREAM_BOUNDARY "\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %zu\r\n\r\n", len);

        esp_err_t res = httpd_resp_send_chunk(req, hdr, hdr_len);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)jpg, (ssize_t)len);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, "\r\n", 2);

        if (converted) free(jpg);
        esp_camera_fb_return(pic);

        if (res != ESP_OK) {
            ESP_LOGI(TG_CTL_STRM, "client disconnect after %lu frames", (unsigned long)frame_idx);
            break;
        }
        frame_idx++;
        if (frame_idx % 30 == 0)
            ESP_LOGI(TG_CTL_STRM, "streaming: %lu frames  heap=%lu B",
                     (unsigned long)frame_idx, (unsigned long)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGI(TG_CTL_STRM, "stream ended: %lu frames  %lld µs",
             (unsigned long)frame_idx, (long long)(esp_timer_get_time() - t0));
    return ESP_OK;
}

esp_err_t pan_handler(httpd_req_t *req) {
    int64_t t0 = esp_timer_get_time();
    log_request(req, TG_CTL_CMD);
    int angle = 90;
    if (query_int(req, "angle", &angle) != ESP_OK) {
        send_err(req, "missing angle"); return ESP_FAIL;
    }
    if (servo_set_pan(angle) != ESP_OK) {
        send_err(req, "servo error"); return ESP_FAIL;
    }
    ESP_LOGI(TG_CTL_CMD, "pan=%d°", servo_get_pan());
    send_ok(req);
    log_response(TG_CTL_CMD, true, 11, t0);
    return ESP_OK;
}

esp_err_t tilt_handler(httpd_req_t *req) {
    int64_t t0 = esp_timer_get_time();
    log_request(req, TG_CTL_CMD);
    int angle = 90;
    if (query_int(req, "angle", &angle) != ESP_OK) {
        send_err(req, "missing angle"); return ESP_FAIL;
    }
    if (servo_set_tilt(angle) != ESP_OK) {
        send_err(req, "servo error"); return ESP_FAIL;
    }
    ESP_LOGI(TG_CTL_CMD, "tilt=%d°", servo_get_tilt());
    send_ok(req);
    log_response(TG_CTL_CMD, true, 11, t0);
    return ESP_OK;
}

esp_err_t led_handler(httpd_req_t *req) {
    int64_t t0 = esp_timer_get_time();
    log_request(req, TG_CTL_CMD);
    int state = 0;
    if (query_int(req, "state", &state) != ESP_OK) {
        send_err(req, "missing state"); return ESP_FAIL;
    }
    if (led_set(state != 0) != ESP_OK) {
        send_err(req, "gpio error"); return ESP_FAIL;
    }
    ESP_LOGI(TG_CTL_CMD, "led=%s", led_get() ? "ON" : "OFF");
    send_ok(req);
    log_response(TG_CTL_CMD, true, 11, t0);
    return ESP_OK;
}

esp_err_t switch_handler(httpd_req_t *req) {
    int64_t t0 = esp_timer_get_time();
    log_request(req, TG_CTL_CMD);
    int state = 0;
    if (query_int(req, "state", &state) != ESP_OK) {
        send_err(req, "missing state"); return ESP_FAIL;
    }
    if (switch_set(state != 0) != ESP_OK) {
        send_err(req, "gpio error"); return ESP_FAIL;
    }
    ESP_LOGI(TG_CTL_CMD, "switch=%s", switch_get() ? "ON" : "OFF");
    send_ok(req);
    log_response(TG_CTL_CMD, true, 11, t0);
    return ESP_OK;
}

esp_err_t periph_state_handler(httpd_req_t *req) {
    int64_t t0 = esp_timer_get_time();
    log_request(req, TG_CTL_CMD);
    char buf[128];
    int n = snprintf(buf, sizeof(buf),
             "{\"led\":%d,\"sw\":%d,\"pan\":%d,\"tilt\":%d}",
             led_get() ? 1 : 0, switch_get() ? 1 : 0,
             servo_get_pan(), servo_get_tilt());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, buf, n);
    log_response(TG_CTL_CMD, true, (size_t)n, t0);
    return ESP_OK;
}

// ─── Handler registration ─────────────────────────────────────────────────────

void controller_register_handlers(httpd_handle_t server) {
    const httpd_uri_t routes[] = {
        { .uri = "/",              .method = HTTP_GET,  .handler = root_handler          },
        { .uri = "/capture",       .method = HTTP_POST, .handler = capture_handler       },
        { .uri = "/stream",        .method = HTTP_GET,  .handler = stream_handler        },
        { .uri = "/status",        .method = HTTP_GET,  .handler = status_handler        },
        { .uri = "/hardware",      .method = HTTP_GET,  .handler = hardware_info_handler },
        { .uri = "/control/pan",   .method = HTTP_GET,  .handler = pan_handler           },
        { .uri = "/control/tilt",  .method = HTTP_GET,  .handler = tilt_handler          },
        { .uri = "/control/led",   .method = HTTP_GET,  .handler = led_handler           },
        { .uri = "/control/switch",.method = HTTP_GET,  .handler = switch_handler        },
        { .uri = "/periph/state",  .method = HTTP_GET,  .handler = periph_state_handler  },
    };
    const int n = (int)(sizeof(routes) / sizeof(routes[0]));
    for (int i = 0; i < n; i++) {
        esp_err_t r = httpd_register_uri_handler(server, &routes[i]);
        if (r != ESP_OK)
            ESP_LOGE(TG_NET_HTTP, "register FAILED %s: %s",
                     routes[i].uri, esp_err_to_name(r));
        else
            ESP_LOGI(TG_NET_HTTP, "registered  %s %s",
                     routes[i].method == HTTP_GET ? "GET " : "POST",
                     routes[i].uri);
    }
    ESP_LOGI(TG_NET_HTTP, "%d routes registered", n);
}

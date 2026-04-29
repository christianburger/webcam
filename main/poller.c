// =============================================================================
//  poller.c  –  ESP32 cloud relay polling task (thread‑safe)
// =============================================================================

#include "poller.h"
#include "peripherals.h"
#include "cam_log.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_camera.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "img_converters.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "camera_safe.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

// ─── PSRAM-aware allocator ────────────────────────────────────────────────────
static void *pmalloc(size_t n) {
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(n, MALLOC_CAP_8BIT);
    if (!p) ESP_LOGE(TG_POLL_TICK, "OOM malloc(%zu)", n);
    return p;
}

// ─── AES-128-CBC encrypt ──────────────────────────────────────────────────────
static esp_err_t aes_encrypt(const uint8_t *in, size_t in_len,
                              uint8_t **out,  size_t *out_len) {
    uint8_t iv[16];
    esp_fill_random(iv, 16);

    size_t pad    = 16 - (in_len % 16);
    size_t padded = in_len + pad;

    uint8_t *plain = pmalloc(padded);
    if (!plain) return ESP_ERR_NO_MEM;
    memcpy(plain, in, in_len);
    memset(plain + in_len, (uint8_t)pad, pad);

    *out = pmalloc(16 + padded);
    if (!*out) { free(plain); return ESP_ERR_NO_MEM; }
    memcpy(*out, iv, 16);

    uint8_t iv_work[16];
    memcpy(iv_work, iv, 16);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int rc = mbedtls_aes_setkey_enc(&aes, (const uint8_t *)POLLER_AES_KEY, 128);
    if (rc == 0)
        rc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT,
                                   padded, iv_work, plain, *out + 16);
    mbedtls_aes_free(&aes);
    free(plain);

    if (rc != 0) {
        free(*out); *out = NULL;
        ESP_LOGE(TG_POLL_TICK, "AES err -0x%04X", (unsigned)(-rc));
        return ESP_FAIL;
    }
    *out_len = 16 + padded;
    return ESP_OK;
}

// ─── Base-64 encode ───────────────────────────────────────────────────────────
static char *b64_encode(const uint8_t *in, size_t in_len, size_t *out_len) {
    size_t req = 0;
    mbedtls_base64_encode(NULL, 0, &req, in, in_len);
    req++;
    char *out = pmalloc(req);
    if (!out) return NULL;
    if (mbedtls_base64_encode((uint8_t *)out, req, out_len, in, in_len) != 0) {
        free(out); return NULL;
    }
    out[*out_len] = '\0';
    return out;
}

// ─── Minimal JSON parsers ─────────────────────────────────────────────────────
static bool json_str(const char *json, const char *key, char *out, size_t sz) {
    char needle[48];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    const char *e = strchr(p, '"');
    if (!e) return false;
    size_t n = (size_t)(e - p);
    if (n >= sz) n = sz - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

static bool json_int(const char *json, const char *key, int *out) {
    char needle[48];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ') p++;
    if (*p == '"') p++;
    *out = atoi(p);
    return true;
}

// ─── Command executor ─────────────────────────────────────────────────────────
static void execute_command(const char *type, int value) {
    if      (strcmp(type, "pan")    == 0) servo_set_pan(value);
    else if (strcmp(type, "tilt")   == 0) servo_set_tilt(value);
    else if (strcmp(type, "led")    == 0) led_set(value != 0);
    else if (strcmp(type, "switch") == 0) switch_set(value != 0);
    else if (strcmp(type, "center") == 0) { servo_set_pan(90); servo_set_tilt(90); }
    else { ESP_LOGW(TG_POLL_CMD, "unknown cmd '%s'", type); return; }
    ESP_LOGI(TG_POLL_CMD, "%s=%d", type, value);
}

static void parse_and_execute_commands(const char *json) {
    const char *arr = strstr(json, "\"commands\":[");
    if (!arr) return;
    arr = strchr(arr, '[');
    if (!arr) return;

    const char *p = arr + 1;
    int n = 0;

    while (*p && *p != ']') {
        const char *obj     = strchr(p, '{');
        const char *bracket = strchr(p, ']');
        if (!obj || (bracket && obj > bracket)) break;
        const char *obj_end = strchr(obj, '}');
        if (!obj_end) break;

        size_t len = (size_t)(obj_end - obj) + 1;
        char *buf  = malloc(len + 1);
        if (!buf) break;
        memcpy(buf, obj, len);
        buf[len] = '\0';

        char type[32] = {0};
        int  value    = 0;
        if (json_str(buf, "type", type, sizeof(type))) {
            json_int(buf, "value", &value);
            execute_command(type, value);
            n++;
        } else {
            ESP_LOGW(TG_POLL_CMD, "object missing 'type': %s", buf);
        }
        free(buf);
        p = obj_end + 1;
    }

    if (n > 0)
        ESP_LOGI(TG_POLL_CMD, "%d command(s) executed", n);
}

// ─── HTTP response accumulator ────────────────────────────────────────────────
#define RESP_MAX 2048

typedef struct {
    char  *buf;
    size_t used;
    size_t cap;
} resp_t;

static esp_err_t on_http_event(esp_http_client_event_t *e) {
    resp_t *r = (resp_t *)e->user_data;
    if (!r || e->event_id != HTTP_EVENT_ON_DATA || e->data_len <= 0) return ESP_OK;
    size_t avail = r->cap - r->used - 1;
    size_t copy  = (size_t)e->data_len < avail ? (size_t)e->data_len : avail;
    if (copy > 0) {
        memcpy(r->buf + r->used, e->data, copy);
        r->used += copy;
        r->buf[r->used] = '\0';
    }
    return ESP_OK;
}

// ─── POST one checkin ─────────────────────────────────────────────────────────
static esp_err_t do_checkin(const char *frame_b64, size_t b64_len) {
    size_t json_cap = b64_len + 256;
    char *body = pmalloc(json_cap);
    if (!body) return ESP_ERR_NO_MEM;

    int body_len = snprintf(body, json_cap,
        "{"
          "\"frame\":\"%s\","
          "\"status\":{"
            "\"pan\":%d,\"tilt\":%d,"
            "\"led\":%d,\"sw\":%d,"
            "\"heap\":%lu,\"uptime\":%llu"
          "}"
        "}",
        frame_b64,
        servo_get_pan(), servo_get_tilt(),
        led_get() ? 1 : 0, switch_get() ? 1 : 0,
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long long)(esp_timer_get_time() / 1000000ULL));

    resp_t resp = { .cap = RESP_MAX };
    resp.buf = calloc(1, RESP_MAX);
    if (!resp.buf) { free(body); return ESP_ERR_NO_MEM; }

    esp_http_client_config_t cfg = {
        .url               = POLLER_BACKEND_URL,
        .method            = HTTP_METHOD_POST,
        .timeout_ms        = POLLER_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler     = on_http_event,
        .user_data         = &resp,
        .buffer_size       = 1024,
        .buffer_size_tx    = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { free(body); free(resp.buf); return ESP_FAIL; }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-Device-Key", POLLER_DEVICE_KEY);
    esp_http_client_set_post_field(client, body, body_len);

    esp_err_t ret = esp_http_client_perform(client);
    free(body);

    if (ret != ESP_OK) {
        ESP_LOGE(TG_POLL_TICK, "HTTP fail: %s", esp_err_to_name(ret));
        esp_http_client_cleanup(client);
        free(resp.buf);
        return ret;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGE(TG_POLL_TICK, "server %d: %s", status, resp.buf);
        free(resp.buf);
        return ESP_FAIL;
    }

    ESP_LOGD(TG_POLL_TICK, "200 OK: %s", resp.buf);
    parse_and_execute_commands(resp.buf);
    free(resp.buf);
    return ESP_OK;
}

// ─── Main polling loop ────────────────────────────────────────────────────────
void poller_task(void *pvParameters) {
    ESP_LOGI(TG_POLL_TICK, "started  interval=%d ms  backend=%s",
             POLLER_INTERVAL_MS, POLLER_BACKEND_URL);

    uint32_t ok_count   = 0;
    uint32_t fail_count = 0;

    vTaskDelay(pdMS_TO_TICKS(3000));   // wait for WiFi + camera to settle

    while (1) {
        TickType_t t0 = xTaskGetTickCount();

        // ── Capture frame using thread‑safe wrapper ─────────────────────────
        camera_fb_t *pic = safe_camera_fb_get();
        if (!pic) {
            ESP_LOGW(TG_POLL_TICK, "fb_get NULL — skip");
            goto next_tick;
        }

        {
            // JPEG encode
            uint8_t *jpg  = NULL;
            size_t   jlen = 0;
            bool     conv = false;

            if (pic->format == PIXFORMAT_JPEG) {
                jpg  = pic->buf;
                jlen = pic->len;
            } else {
                if (!frame2jpg(pic, 80, &jpg, &jlen)) {
                    ESP_LOGE(TG_POLL_TICK, "JPEG encode failed");
                    safe_camera_fb_return(pic);
                    goto next_tick;
                }
                conv = true;
            }

            // AES encrypt
            uint8_t *enc  = NULL;
            size_t   elen = 0;
            if (aes_encrypt(jpg, jlen, &enc, &elen) != ESP_OK) {
                if (conv) free(jpg);
                safe_camera_fb_return(pic);
                goto next_tick;
            }
            if (conv) free(jpg);

            // Return frame buffer NOW (thread‑safe)
            safe_camera_fb_return(pic);

            // Base64 encode
            size_t b64len = 0;
            char  *b64    = b64_encode(enc, elen, &b64len);
            free(enc);
            if (!b64) {
                ESP_LOGE(TG_POLL_TICK, "base64 OOM");
                goto next_tick;
            }

            ESP_LOGD(TG_POLL_TICK, "jpg=%zub enc=%zub b64=%zub", jlen, elen, b64len);

            // POST
            esp_err_t ret = do_checkin(b64, b64len);
            free(b64);

            if (ret == ESP_OK) {
                ok_count++;
            } else {
                fail_count++;
                ESP_LOGW(TG_POLL_TICK, "checkin failed (ok=%lu fail=%lu)",
                         ok_count, fail_count);
            }

            LOG_EVERY(TG_POLL_TICK, 20, (ok_count + fail_count),
                      "stats ok=%lu fail=%lu heap=%lu B",
                      ok_count, fail_count, (unsigned long)esp_get_free_heap_size());
        }

next_tick:;
        TickType_t elapsed = xTaskGetTickCount() - t0;
        TickType_t period  = pdMS_TO_TICKS(POLLER_INTERVAL_MS);
        if (elapsed < period) vTaskDelay(period - elapsed);
        else                  taskYIELD();
    }
}
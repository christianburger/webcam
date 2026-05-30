// =============================================================================
//  main.c  –  ESP32 Camera Server
//
//  Thread-safety model
//  ───────────────────
//  frame_queue   single-producer (camera_task), single-consumer (stream_handler).
//                Non-blocking send: frame dropped if consumer is slow.
//                Depth = 1: guarantees free buffers for DMA while streaming.
//
//  esp_camera    fb_get / fb_return are internally thread-safe in the Espressif
//                driver. An external mutex that holds a lock ACROSS a blocking
//                fb_get() call is a reliable deadlock.
//
//  Buffer pool accounting
//  ──────────────────────
//  Invariant:  queue_depth  <  fb_count - 1
//              1            <  3 - 1 = 2   ✓
// =============================================================================

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "network_manager.h"
#include "cam_log.h"

// ─── Camera pin map  (AI-Thinker / ESP32-CAM) ────────────────────────────────
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

#define CAM_INIT_MAX_RETRIES    3
#define CAM_INIT_RETRY_DELAY_MS 2000

// 0 = normal, 1 = flipped — adjust to match physical mounting
#define CAM_HMIRROR  0
#define CAM_VFLIP    0

// ─── Shared state ─────────────────────────────────────────────────────────────
QueueHandle_t frame_queue;

// ─── Camera configuration – tuned for GC2145 stability ───────────────────────
static camera_config_t camera_config = {
    .pin_pwdn       = CAM_PIN_PWDN,
    .pin_reset      = CAM_PIN_RESET,
    .pin_xclk       = CAM_PIN_XCLK,
    .pin_sccb_sda   = CAM_PIN_SIOD,
    .pin_sccb_scl   = CAM_PIN_SIOC,
    .pin_d7         = CAM_PIN_D7,
    .pin_d6         = CAM_PIN_D6,
    .pin_d5         = CAM_PIN_D5,
    .pin_d4         = CAM_PIN_D4,
    .pin_d3         = CAM_PIN_D3,
    .pin_d2         = CAM_PIN_D2,
    .pin_d1         = CAM_PIN_D1,
    .pin_d0         = CAM_PIN_D0,
    .pin_vsync      = CAM_PIN_VSYNC,
    .pin_href       = CAM_PIN_HREF,
    .pin_pclk       = CAM_PIN_PCLK,
    .xclk_freq_hz   = 5000000,             // 5 MHz – dramatically reduces DMA overflows
    .ledc_timer     = LEDC_TIMER_0,
    .ledc_channel   = LEDC_CHANNEL_0,
    .pixel_format   = PIXFORMAT_RGB565,
    .frame_size     = FRAMESIZE_QVGA,      // 320×240
    .jpeg_quality   = 12,
    .fb_count       = 3,                   // Extra buffer for DMA smoothness
    .fb_location    = CAMERA_FB_IN_PSRAM,
    .grab_mode      = CAMERA_GRAB_LATEST,
};

// ─── Helper: Get sensor name for logging ─────────────────────────────────────
static const char *sensor_name(uint16_t pid) {
    switch (pid) {
        case 0x2641: case 0x2642: return "OV2640";
        case 0x7673: return "OV7670";
        case 0x7721: return "OV7725";
        case 0x3660: return "OV3660";
        case 0x5640: return "OV5640";
        case 0x1410: return "NT99141";
        case 0x2145: return "GC2145";
        case 0x232A: return "GC032A";
        case 0x9B46: return "GC0308";
        default:     return "unknown";
    }
}

// ─── Camera Hardware Initialization (robust power‑up) ─────────────────────────
static esp_err_t camera_init_with_reset_sequence(void) {
    esp_err_t err = ESP_FAIL;

    ESP_LOGI(TG_CAM_INIT, "Starting camera hardware power‑up sequence...");

    // 1. Assert PWDN
    if (camera_config.pin_pwdn >= 0) {
        ESP_LOGI(TG_CAM_INIT, "Asserting PWDN (pin %d) to power down sensor...",
                 camera_config.pin_pwdn);
        gpio_config_t pwdn_cfg = {
            .pin_bit_mask = (1ULL << camera_config.pin_pwdn),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        err = gpio_config(&pwdn_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TG_CAM_INIT, "Failed to configure PWDN pin %d: %s",
                     camera_config.pin_pwdn, esp_err_to_name(err));
            return err;
        }
        err = gpio_set_level(camera_config.pin_pwdn, 1);
        if (err != ESP_OK) {
            ESP_LOGE(TG_CAM_INIT, "Failed to set PWDN high: %s", esp_err_to_name(err));
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // 2. Assert RESET if defined
    if (camera_config.pin_reset >= 0) {
        ESP_LOGI(TG_CAM_INIT, "Asserting RESET (pin %d) for complete reset...",
                 camera_config.pin_reset);
        gpio_config_t reset_cfg = {
            .pin_bit_mask = (1ULL << camera_config.pin_reset),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        err = gpio_config(&reset_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TG_CAM_INIT, "Failed to configure RESET pin %d: %s",
                     camera_config.pin_reset, esp_err_to_name(err));
            return err;
        }
        err = gpio_set_level(camera_config.pin_reset, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TG_CAM_INIT, "Failed to set RESET low: %s", esp_err_to_name(err));
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        err = gpio_set_level(camera_config.pin_reset, 1);
        if (err != ESP_OK) {
            ESP_LOGE(TG_CAM_INIT, "Failed to set RESET high: %s", esp_err_to_name(err));
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // 3. De‑assert PWDN
    if (camera_config.pin_pwdn >= 0) {
        ESP_LOGI(TG_CAM_INIT, "Releasing PWDN to power up sensor...");
        err = gpio_set_level(camera_config.pin_pwdn, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TG_CAM_INIT, "Failed to set PWDN low: %s", esp_err_to_name(err));
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // 4. Critical stabilisation delay
    ESP_LOGI(TG_CAM_INIT, "Waiting for internal oscillator to stabilise (100 ms)...");
    vTaskDelay(pdMS_TO_TICKS(100));

    // 5. Initialise camera driver
    ESP_LOGI(TG_CAM_INIT, "Calling esp_camera_init()...");
    err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TG_CAM_INIT, "esp_camera_init() failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TG_CAM_INIT, "Camera hardware initialised successfully.");
    return ESP_OK;
}

// ─── Camera Task ──────────────────────────────────────────────────────────────
void camera_task(void *pvParameters) {

    ESP_LOGI(TG_CAM_INIT, "PSRAM %zu KB  heap %lu B",
             esp_psram_get_size() / 1024, (unsigned long)esp_get_free_heap_size());

    // Retry initialisation
    esp_err_t err = ESP_FAIL;
    for (int i = 1; i <= CAM_INIT_MAX_RETRIES && err != ESP_OK; i++) {
        err = camera_init_with_reset_sequence();
        if (err != ESP_OK) {
            ESP_LOGW(TG_CAM_INIT, "Init attempt %d/%d failed: %s",
                     i, CAM_INIT_MAX_RETRIES, esp_err_to_name(err));
            if (i < CAM_INIT_MAX_RETRIES) vTaskDelay(pdMS_TO_TICKS(CAM_INIT_RETRY_DELAY_MS));
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TG_CAM_INIT, "All camera init attempts failed — task suspended");
        vTaskSuspend(NULL);
        return;
    }

    // Sensor configuration
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        ESP_LOGE(TG_CAM_INIT, "Failed to get sensor descriptor");
        vTaskSuspend(NULL);
        return;
    }

    uint16_t pid = s->id.PID;
    ESP_LOGI(TG_CAM_INIT, "sensor %s PID=0x%04X  fs=%d q=%d awb=%d aec=%d",
             sensor_name(pid), pid,
             s->status.framesize, s->status.quality,
             s->status.awb, s->status.aec);

    // Force QVGA if driver misbehaves
    if (s->status.framesize != FRAMESIZE_QVGA) {
        ESP_LOGW(TG_CAM_INIT, "Current framesize is %d, forcing QVGA...", s->status.framesize);
        s->set_framesize(s, FRAMESIZE_QVGA);
    }

    if (pid == 0x2145) {
        ESP_LOGI(TG_CAM_INIT, "Sensor is GC2145: using RGB565 + software JPEG conversion");
    }

    // Apply mirror/flip (ignore non‑critical failures)
    s->set_hmirror(s, CAM_HMIRROR);
    s->set_vflip(s, CAM_VFLIP);
    s->set_whitebal(s, 1);      // may fail – safe to ignore
    s->set_exposure_ctrl(s, 1); // may fail
    s->set_gain_ctrl(s, 1);     // may fail
    ESP_LOGI(TG_CAM_INIT, "Post‑init settings applied: hmirror=%d vflip=%d",
             CAM_HMIRROR, CAM_VFLIP);

    // Warm‑up with aggressive retries
    vTaskDelay(pdMS_TO_TICKS(300));
    camera_fb_t *wb = NULL;
    for (int i = 0; i < 20; i++) {
        wb = esp_camera_fb_get();
        if (wb) {
            ESP_LOGI(TG_CAM_INIT, "Warm-up frame %zu B OK", wb->len);
            esp_camera_fb_return(wb);
            break;
        }
        int delay = 50 + (i * 10);
        ESP_LOGW(TG_CAM_INIT, "Warm-up attempt %d/20 failed, retrying in %d ms...", i+1, delay);
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
    if (!wb) {
        ESP_LOGW(TG_CAM_INIT, "All warm-up attempts failed – continuing anyway");
    }

    ESP_LOGI(TG_CAM_INIT, "Capture loop started on core %d", xPortGetCoreID());

    uint32_t frame_count = 0;
    uint32_t drop_count  = 0;

    while (1) {
        camera_fb_t *pic = esp_camera_fb_get();
        if (!pic) {
            ESP_LOGW(TG_CAM_STRM, "fb_get NULL – possible DMA overflow");
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        frame_count++;

        if (xQueueSend(frame_queue, &pic, 0) != pdTRUE) {
            drop_count++;
            esp_camera_fb_return(pic);
        }

        LOG_EVERY(TG_CAM_STRM, 60, frame_count,
                  "frames=%lu drops=%lu heap=%lu B",
                  (unsigned long)frame_count, (unsigned long)drop_count,
                  (unsigned long)esp_get_free_heap_size());

        // Critical: 20 ms delay prevents DMA overrun and gives Wi‑Fi time to breathe
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ─── app_main ─────────────────────────────────────────────────────────────────
void app_main(void) {
    ESP_LOGI(TG_CAM_INIT, "ESP32 Camera Server starting");
    ESP_LOGI(TG_CAM_INIT, "heap=%lu B  PSRAM=%zu B",
             (unsigned long)esp_get_free_heap_size(), esp_psram_get_size());

    // Queue depth 1 with fb_count=3 is safe
    frame_queue = xQueueCreate(1, sizeof(camera_fb_t *));
    if (!frame_queue) {
        ESP_LOGE(TG_CAM_INIT, "frame_queue create failed — abort");
        return;
    }

    BaseType_t r;
    r = xTaskCreatePinnedToCore(network_task, "net", 8192, NULL,
                                configMAX_PRIORITIES - 1, NULL, 1);
    if (r != pdPASS) ESP_LOGE(TG_CAM_INIT, "network_task create FAILED");

    r = xTaskCreatePinnedToCore(camera_task, "cam", 8192, NULL,
                                configMAX_PRIORITIES - 2, NULL, 0);
    if (r != pdPASS) ESP_LOGE(TG_CAM_INIT, "camera_task create FAILED");
}
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
//              1            <  2 - 1 = 1   ✓ (queue_depth must be ≤ fb_count-2)
//  With fb_count=2, queue depth 1 is safe.
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
// frame_queue  –  stream_handler is the ONLY consumer.
QueueHandle_t frame_queue;

// ─── Camera configuration structure (optimised for GC2145) ────────────────────
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
    .xclk_freq_hz   = 10000000,            // Reduced from 20 MHz for GC2145 stability
    .ledc_timer     = LEDC_TIMER_0,
    .ledc_channel   = LEDC_CHANNEL_0,
    .pixel_format   = PIXFORMAT_RGB565,    // GC2145 does not support hardware JPEG
    .frame_size     = FRAMESIZE_VGA,
    .jpeg_quality   = 12,
    .fb_count       = 2,                   // Two buffers allow one to be queued
    .fb_location    = CAMERA_FB_IN_PSRAM,
    .grab_mode      = CAMERA_GRAB_WHEN_EMPTY, // Prevents stale frame corruption
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

// ─── Camera Hardware Initialization (with full error checking) ────────────────
static esp_err_t camera_init_with_reset_sequence(void) {
    esp_err_t err = ESP_FAIL;

    ESP_LOGI(TG_CAM_INIT, "Starting camera hardware power‑up sequence...");

    // 1. Assert PWDN (power down) if pin is valid
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

    // 2. Assert RESET if defined and valid
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

    // 3. De-assert PWDN to power up the sensor
    if (camera_config.pin_pwdn >= 0) {
        ESP_LOGI(TG_CAM_INIT, "Releasing PWDN to power up sensor...");
        err = gpio_set_level(camera_config.pin_pwdn, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TG_CAM_INIT, "Failed to set PWDN low: %s", esp_err_to_name(err));
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // 4. Wait for internal oscillator to stabilise (CRITICAL for GC2145)
    ESP_LOGI(TG_CAM_INIT, "Waiting for internal oscillator to stabilise (100 ms)...");
    vTaskDelay(pdMS_TO_TICKS(100));

    // 5. Attempt camera driver initialisation
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

    // 1. PSRAM check
    ESP_LOGI(TG_CAM_INIT, "PSRAM %zu KB  heap %lu B",
             esp_psram_get_size() / 1024, (unsigned long)esp_get_free_heap_size());

    // 2. Camera init (with robust power‑up sequence)
    esp_err_t err = ESP_FAIL;
    for (int i = 1; i <= CAM_INIT_MAX_RETRIES && err != ESP_OK; i++) {
        err = camera_init_with_reset_sequence();
        if (err != ESP_OK) {
            ESP_LOGW(TG_CAM_INIT, "Init attempt %d/%d failed: %s",
                     i, CAM_INIT_MAX_RETRIES, esp_err_to_name(err));
            if (i < CAM_INIT_MAX_RETRIES) {
                vTaskDelay(pdMS_TO_TICKS(CAM_INIT_RETRY_DELAY_MS));
            }
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TG_CAM_INIT, "All camera init attempts failed — task suspended");
        vTaskSuspend(NULL);
        return;
    }

    ESP_LOGI(TG_CAM_INIT, "esp_camera_init OK  xclk=%d Hz  fs=%d  fb=%d",
             camera_config.xclk_freq_hz, camera_config.frame_size, camera_config.fb_count);

    // 3. Sensor ID and configuration
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

    // 4. Log hardware JPEG capability (GC2145 does NOT support hardware JPEG)
    if (pid == 0x2145) {
        ESP_LOGI(TG_CAM_INIT, "Sensor is GC2145: hardware JPEG NOT supported — using RGB565 + software conversion");
    } else {
        ESP_LOGI(TG_CAM_INIT, "Sensor supports hardware JPEG — using PIXFORMAT_JPEG for better performance");
        // For sensors that support JPEG, re-initialise with JPEG format
        if (camera_config.pixel_format != PIXFORMAT_JPEG) {
            ESP_LOGI(TG_CAM_INIT, "Re‑initialising with PIXFORMAT_JPEG...");
            err = esp_camera_deinit();
            if (err != ESP_OK) {
                ESP_LOGW(TG_CAM_INIT, "esp_camera_deinit() failed: %s", esp_err_to_name(err));
            } else {
                camera_config.pixel_format = PIXFORMAT_JPEG;
                err = camera_init_with_reset_sequence();
                if (err != ESP_OK) {
                    ESP_LOGW(TG_CAM_INIT, "JPEG re‑init failed, falling back to RGB565");
                    camera_config.pixel_format = PIXFORMAT_RGB565;
                } else {
                    ESP_LOGI(TG_CAM_INIT, "Hardware JPEG enabled successfully");
                    s = esp_camera_sensor_get();
                }
            }
        }
    }

    // 5. Apply mirror/flip and other controls (with error checking)
    if (s) {
        err = s->set_hmirror(s, CAM_HMIRROR);
        if (err != ESP_OK) ESP_LOGW(TG_CAM_INIT, "set_hmirror failed: %s", esp_err_to_name(err));
        err = s->set_vflip(s, CAM_VFLIP);
        if (err != ESP_OK) ESP_LOGW(TG_CAM_INIT, "set_vflip failed: %s", esp_err_to_name(err));
        err = s->set_whitebal(s, 1);
        if (err != ESP_OK) ESP_LOGW(TG_CAM_INIT, "set_whitebal failed: %s", esp_err_to_name(err));
        err = s->set_exposure_ctrl(s, 1);
        if (err != ESP_OK) ESP_LOGW(TG_CAM_INIT, "set_exposure_ctrl failed: %s", esp_err_to_name(err));
        err = s->set_gain_ctrl(s, 1);
        if (err != ESP_OK) ESP_LOGW(TG_CAM_INIT, "set_gain_ctrl failed: %s", esp_err_to_name(err));
        ESP_LOGI(TG_CAM_INIT, "Post‑init settings applied: hmirror=%d vflip=%d whitebal=1 exposure=1 gain=1",
                 CAM_HMIRROR, CAM_VFLIP);
    }

    // 6. Warm-up frame (with retry logic – 10 attempts)
    vTaskDelay(pdMS_TO_TICKS(300));
    camera_fb_t *wb = NULL;
    for (int i = 0; i < 10; i++) {
        wb = esp_camera_fb_get();
        if (wb) {
            ESP_LOGI(TG_CAM_INIT, "Warm-up frame %zu B OK", wb->len);
            esp_camera_fb_return(wb);  // void function, no return value to check
            break;
        }
        ESP_LOGW(TG_CAM_INIT, "Warm-up frame attempt %d/10 failed, retrying...", i+1);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (!wb) {
        ESP_LOGW(TG_CAM_INIT, "All warm-up attempts failed — continuing anyway");
    }

    ESP_LOGI(TG_CAM_INIT, "Capture loop started on core %d", xPortGetCoreID());

    // 7. Main capture loop
    uint32_t frame_count = 0;
    uint32_t drop_count  = 0;

    while (1) {
        camera_fb_t *pic = esp_camera_fb_get();
        if (!pic) {
            ESP_LOGW(TG_CAM_STRM, "fb_get NULL — possible DMA/VSYNC loss");
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        frame_count++;

        // Non-blocking send: drop frame if stream_handler hasn't consumed the previous one.
        if (xQueueSend(frame_queue, &pic, 0) != pdTRUE) {
            drop_count++;
            esp_camera_fb_return(pic);  // void function
        }

        LOG_EVERY(TG_CAM_STRM, 60, frame_count,
                  "frames=%lu drops=%lu heap=%lu B",
                  (unsigned long)frame_count, (unsigned long)drop_count,
                  (unsigned long)esp_get_free_heap_size());

        vTaskDelay(pdMS_TO_TICKS(50));   // ~20 fps ceiling; yields to WiFi/HTTP
    }
}

// ─── app_main ─────────────────────────────────────────────────────────────────
void app_main(void) {
    ESP_LOGI(TG_CAM_INIT, "ESP32 Camera Server starting");
    ESP_LOGI(TG_CAM_INIT, "heap=%lu B  PSRAM=%zu B",
             (unsigned long)esp_get_free_heap_size(), esp_psram_get_size());

    // Queue depth must be less than fb_count-1. With fb_count=2, max depth is 1.
    frame_queue = xQueueCreate(1, sizeof(camera_fb_t *));
    if (!frame_queue) {
        ESP_LOGE(TG_CAM_INIT, "frame_queue create failed — abort");
        return;
    }

    BaseType_t r;

    r = xTaskCreatePinnedToCore(
            network_task, "net", NETWORK_TASK_STACK_SIZE, NULL,
            configMAX_PRIORITIES - 1, NULL, 1);
    if (r != pdPASS) ESP_LOGE(TG_CAM_INIT, "network_task create FAILED");

    r = xTaskCreatePinnedToCore(
            camera_task, "cam", 8192, NULL,
            configMAX_PRIORITIES - 2, NULL, 0);
    if (r != pdPASS) ESP_LOGE(TG_CAM_INIT, "camera_task create FAILED");
}
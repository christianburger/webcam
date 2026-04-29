// =============================================================================
//  main.c  –  ESP32 Camera Server with mutex + manual exposure
// =============================================================================

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_system.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "network_manager.h"
#include "cam_log.h"
#include "esp_timer.h"
#include "camera_safe.h"

// ─── Camera pin map  (AI-Thinker / ESP32-CAM) ─────────────────────────────────
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

#define GC2145_SCCB_ADDR  0x3C
#define CAM_INIT_MAX_RETRIES    3
#define CAM_INIT_RETRY_DELAY_MS 2000
#define CAM_HMIRROR  0
#define CAM_VFLIP    1

QueueHandle_t frame_queue;
SemaphoreHandle_t camera_mutex;   // defined here, exported via header

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
    .xclk_freq_hz   = 20000000,
    .ledc_timer     = LEDC_TIMER_0,
    .ledc_channel   = LEDC_CHANNEL_0,
    .pixel_format   = PIXFORMAT_JPEG,
    .frame_size     = FRAMESIZE_VGA,
    //.frame_size     = FRAMESIZE_UXGA,
    .jpeg_quality   = 12,
    .fb_count       = 2,
    .grab_mode      = CAMERA_GRAB_LATEST,
};

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

static void i2c_scan_sccb_bus(void) {
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = CAM_PIN_SIOD,
        .scl_io_num       = CAM_PIN_SIOC,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_param_config(I2C_NUM_1, &conf);
    if (i2c_driver_install(I2C_NUM_1, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) return;

    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        if (i2c_master_cmd_begin(I2C_NUM_1, cmd, pdMS_TO_TICKS(10)) == ESP_OK) {
            ESP_LOGD(TG_CAM_INIT, "I2C 0x%02X%s", addr,
                     (addr == GC2145_SCCB_ADDR) ? " ← expected" : "");
            found++;
        }
        i2c_cmd_link_delete(cmd);
    }
    i2c_driver_delete(I2C_NUM_1);
    if (found == 0)
        ESP_LOGW(TG_CAM_INIT, "I2C scan: no devices");
    else
        ESP_LOGI(TG_CAM_INIT, "I2C scan: %d device(s)", found);
}

// Thread‑safe wrappers (exported via header)
camera_fb_t* safe_camera_fb_get(void) {
    xSemaphoreTake(camera_mutex, portMAX_DELAY);
    camera_fb_t *fb = esp_camera_fb_get();
    xSemaphoreGive(camera_mutex);
    return fb;
}

void safe_camera_fb_return(camera_fb_t *fb) {
    xSemaphoreTake(camera_mutex, portMAX_DELAY);
    esp_camera_fb_return(fb);
    xSemaphoreGive(camera_mutex);
}

void camera_task(void *pvParameters) {
    if (CAM_PIN_PWDN >= 0) {
        gpio_set_direction(CAM_PIN_PWDN, GPIO_MODE_OUTPUT);
        gpio_set_level(CAM_PIN_PWDN, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(CAM_PIN_PWDN, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    i2c_scan_sccb_bus();

    size_t psram = esp_psram_get_size();
    ESP_LOGI(TG_CAM_INIT, "PSRAM %zu KB  heap %lu B", psram/1024, esp_get_free_heap_size());

    esp_err_t err = ESP_FAIL;
    for (int i = 1; i <= CAM_INIT_MAX_RETRIES && err != ESP_OK; i++) {
        err = esp_camera_init(&camera_config);
        if (err != ESP_OK) {
            ESP_LOGW(TG_CAM_INIT, "init attempt %d/%d: %s",
                     i, CAM_INIT_MAX_RETRIES, esp_err_to_name(err));
            if (i < CAM_INIT_MAX_RETRIES) vTaskDelay(pdMS_TO_TICKS(CAM_INIT_RETRY_DELAY_MS));
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TG_CAM_INIT, "camera init failed – task suspended");
        vTaskSuspend(NULL);
        return;
    }
    ESP_LOGI(TG_CAM_INIT, "esp_camera_init OK (fb_count=%d)", camera_config.fb_count);

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        uint16_t pid = s->id.PID;
        ESP_LOGI(TG_CAM_INIT, "sensor %s PID=0x%04X", sensor_name(pid), pid);

        // Orientation
        s->set_hmirror(s, CAM_HMIRROR);
        s->set_vflip(s, CAM_VFLIP);
        s->set_whitebal(s, 1);

        // ─── MANUAL EXPOSURE (fixes long exposure times) ───
        s->set_exposure_ctrl(s, 0);   // disable auto exposure
        s->set_aec_value(s, 800);     // higher = brighter (0‑1200)
        // ─── MANUAL GAIN (reduces noise) ───
        s->set_gain_ctrl(s, 0);       // disable auto gain
        s->set_agc_gain(s, 0);        // gain ceiling (0‑30, 0 = min)
        ESP_LOGI(TG_CAM_INIT, "Manual exposure = 800, gain = 0");
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
    bool warm_ok = false;
    for (int i = 0; i < 3; i++) {
        camera_fb_t *wb = safe_camera_fb_get();
        if (wb) {
            ESP_LOGI(TG_CAM_INIT, "warm-up frame %d OK (%zu B)", i+1, wb->len);
            safe_camera_fb_return(wb);
            warm_ok = true;
            break;
        }
        ESP_LOGW(TG_CAM_INIT, "warm-up frame %d NULL", i+1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!warm_ok) ESP_LOGE(TG_CAM_INIT, "All warm‑up frames failed");

    ESP_LOGI(TG_CAM_INIT, "capture loop started on core %d", xPortGetCoreID());

    uint32_t frame_count = 0;
    uint32_t drop_count = 0;
    int consecutive_failures = 0;

    while (1) {
        int64_t before = esp_timer_get_time();
        ESP_LOGD(TG_CAM_STRM, "fb_get attempt #%lu, heap=%lu, task=%s",
                 (unsigned long)(frame_count+1), (unsigned long)esp_get_free_heap_size(),
                 pcTaskGetName(NULL));

        camera_fb_t *pic = safe_camera_fb_get();
        int64_t after = esp_timer_get_time();

        if (!pic) {
            consecutive_failures++;
            ESP_LOGW(TG_CAM_STRM, "fb_get NULL after %lld µs (fail #%d)",
                     (long long)(after-before), consecutive_failures);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        consecutive_failures = 0;
        frame_count++;
        ESP_LOGI(TG_CAM_STRM, "fb_get OK: %zu B in %lld µs", pic->len, (long long)(after-before));

        if (xQueueSend(frame_queue, &pic, 0) != pdTRUE) {
            drop_count++;
            ESP_LOGW(TG_CAM_STRM, "queue full, dropping frame");
            safe_camera_fb_return(pic);
        } else {
            ESP_LOGD(TG_CAM_STRM, "frame enqueued");
        }

        LOG_EVERY(TG_CAM_STRM, 60, frame_count,
                  "frames=%lu drops=%lu heap=%lu",
                  (unsigned long)frame_count, (unsigned long)drop_count,
                  (unsigned long)esp_get_free_heap_size());

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void) {
    ESP_LOGI(TG_CAM_INIT, "ESP32 Camera Server starting");
    ESP_LOGI(TG_CAM_INIT, "heap=%lu B  PSRAM=%zu B",
             (unsigned long)esp_get_free_heap_size(), esp_psram_get_size());

    // Create mutex for camera access
    camera_mutex = xSemaphoreCreateMutex();
    if (!camera_mutex) {
        ESP_LOGE(TG_CAM_INIT, "Failed to create camera_mutex");
        return;
    }

    // Enable debug logs
    esp_log_level_set("camera", ESP_LOG_DEBUG);
    esp_log_level_set("cam_hal", ESP_LOG_DEBUG);
    esp_log_level_set("ov2640", ESP_LOG_DEBUG);
    esp_log_level_set("sccb", ESP_LOG_DEBUG);
    esp_log_level_set("cam|strm", ESP_LOG_DEBUG);
    esp_log_level_set("ctl|strm", ESP_LOG_DEBUG);
    esp_log_level_set("ctl|capt", ESP_LOG_DEBUG);

    frame_queue = xQueueCreate(2, sizeof(camera_fb_t *));
    if (!frame_queue) {
        ESP_LOGE(TG_CAM_INIT, "frame_queue create failed");
        return;
    }

    BaseType_t r;
    r = xTaskCreatePinnedToCore(network_task, "net", NETWORK_TASK_STACK_SIZE, NULL,
                                configMAX_PRIORITIES - 1, NULL, 1);
    if (r != pdPASS) ESP_LOGE(TG_CAM_INIT, "network_task create FAILED");

    r = xTaskCreatePinnedToCore(camera_task, "cam", 8192, NULL,
                                configMAX_PRIORITIES - 2, NULL, 0);
    if (r != pdPASS) ESP_LOGE(TG_CAM_INIT, "camera_task create FAILED");
}
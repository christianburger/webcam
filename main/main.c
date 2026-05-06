// =============================================================================
//  main.c  –  ESP32 Camera Server
//
//  Thread-safety model
//  ───────────────────
//  frame_queue   single-producer (camera_task), single-consumer (stream_handler).
//                Non-blocking send: frame dropped if consumer is slow.
//                Depth = 1: guarantees free buffers for DMA while streaming.
//
//
//  esp_camera    fb_get / fb_return are internally thread-safe in the Espressif
//                driver. An external mutex that holds a lock ACROSS a blocking
//                fb_get() call is a reliable deadlock:
//
//                  camera_task  holds mutex, blocks in fb_get() waiting for a
//                               free buffer — but both buffers are in the queue.
//                  stream_handler not running → nobody calls fb_return → DEADLOCK.
//
//                camera_safe.{c,h} and camera_mutex are removed entirely.
//                Delete those two files from the source tree.
//
//  Buffer pool accounting
//  ──────────────────────
//  Invariant:  queue_depth  <  fb_count - 1
//              1            <  3 - 1 = 2   ✓
//
//  Worst case: fb[0] → queue (stream_handler consuming)
//              fb[1] → DMA   (camera_task capturing next frame)
//              fb[2] → free / available for next capture
//
//  This eliminates the "cam_hal: Failed to get frame: timeout" seen when
//  fb_count=2 / queue_depth=2 stranded all buffers in the queue.
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
#include "driver/i2c.h"
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

#define GC2145_SCCB_ADDR        0x3C
#define CAM_INIT_MAX_RETRIES    3
#define CAM_INIT_RETRY_DELAY_MS 2000

// 0 = normal, 1 = flipped — adjust to match physical mounting
#define CAM_HMIRROR  0
#define CAM_VFLIP    1

// ─── Shared state ─────────────────────────────────────────────────────────────
// frame_queue  –  stream_handler is the ONLY consumer.
QueueHandle_t frame_queue;

// ─── Camera configuration ─────────────────────────────────────────────────────
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
    .jpeg_quality   = 12,
    .fb_count       = 3,               // ← was 2; see buffer accounting in header
    .grab_mode      = CAMERA_GRAB_LATEST,
};

// ─── Helpers ──────────────────────────────────────────────────────────────────

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

// Pre-init I2C scan – result at DEBUG level; summary at INFO.
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
    // If SCCB driver already owns the bus (post-init call), install fails silently.
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
        ESP_LOGW(TG_CAM_INIT, "I2C scan: no devices — check PWDN, SDA/SCL pull-ups");
    else
        ESP_LOGI(TG_CAM_INIT, "I2C scan: %d device(s)", found);
}

// ─── Camera task ──────────────────────────────────────────────────────────────

void camera_task(void *pvParameters) {

    // 1. Power-cycle sensor
    if (CAM_PIN_PWDN >= 0) {
        gpio_set_direction(CAM_PIN_PWDN, GPIO_MODE_OUTPUT);
        gpio_set_level(CAM_PIN_PWDN, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(CAM_PIN_PWDN, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // 2. Pre-init I2C scan
    i2c_scan_sccb_bus();

    // 3. PSRAM check
    ESP_LOGI(TG_CAM_INIT, "PSRAM %zu KB  heap %lu B",
             esp_psram_get_size() / 1024, (unsigned long)esp_get_free_heap_size());

    // 4. Camera init (with retries)
    esp_err_t err = ESP_FAIL;
    for (int i = 1; i <= CAM_INIT_MAX_RETRIES && err != ESP_OK; i++) {
        err = esp_camera_init(&camera_config);
        if (err != ESP_OK) {
            ESP_LOGW(TG_CAM_INIT, "init attempt %d/%d: %s",
                     i, CAM_INIT_MAX_RETRIES, esp_err_to_name(err));
            if (i < CAM_INIT_MAX_RETRIES)
                vTaskDelay(pdMS_TO_TICKS(CAM_INIT_RETRY_DELAY_MS));
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TG_CAM_INIT, "all init attempts failed — task suspended");
        vTaskSuspend(NULL);
        return;
    }
    ESP_LOGI(TG_CAM_INIT, "esp_camera_init OK  xclk=%d Hz  fs=%d  q=%d  fb=%d",
             camera_config.xclk_freq_hz, camera_config.frame_size,
             camera_config.jpeg_quality, camera_config.fb_count);

    // 5. Sensor ID + post-init settings
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        uint16_t pid = s->id.PID;
        ESP_LOGI(TG_CAM_INIT, "sensor %s PID=0x%04X  fs=%d q=%d awb=%d aec=%d",
                 sensor_name(pid), pid,
                 s->status.framesize, s->status.quality,
                 s->status.awb, s->status.aec);
        s->set_hmirror(s, CAM_HMIRROR);
        s->set_vflip(s, CAM_VFLIP);
        s->set_whitebal(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_gain_ctrl(s, 1);
    }

    // 6. Warm-up frame (let sensor stabilise after XCLK start)
    vTaskDelay(pdMS_TO_TICKS(300));
    camera_fb_t *wb = esp_camera_fb_get();
    if (wb) {
        ESP_LOGI(TG_CAM_INIT, "warm-up frame %zu B OK", wb->len);
        esp_camera_fb_return(wb);
    } else {
        ESP_LOGW(TG_CAM_INIT, "warm-up frame NULL");
    }

    ESP_LOGI(TG_CAM_INIT, "capture loop started on core %d", xPortGetCoreID());

    // 7. Main capture loop
    uint32_t frame_count = 0;
    uint32_t drop_count  = 0;

    while (1) {
        camera_fb_t *pic = esp_camera_fb_get();
        if (!pic) {
            ESP_LOGW(TG_CAM_STRM, "fb_get NULL");
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        frame_count++;

        // Non-blocking: drop frame if stream_handler hasn't consumed the previous one.
        // NEVER block here — that would hold a buffer indefinitely and starve DMA.
        if (xQueueSend(frame_queue, &pic, 0) != pdTRUE) {
            drop_count++;
            esp_camera_fb_return(pic);
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

    // Depth=1  →  queue_depth < fb_count - 1  (1 < 2) ✓
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

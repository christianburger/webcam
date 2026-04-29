#include "peripherals.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "peripherals";

// ─── Cached state (returned by *_get helpers) ────────────────────────────────
static int  s_pan_angle  = SERVO_ANGLE_CENTER;
static int  s_tilt_angle = SERVO_ANGLE_CENTER;
static bool s_led_on     = false;
static bool s_switch_on  = false;

// ─── Internal helpers ─────────────────────────────────────────────────────────

/** Convert an angle (0–180 °) to a 14-bit LEDC duty cycle. */
static uint32_t angle_to_duty(int angle) {
    if (angle < SERVO_ANGLE_MIN) angle = SERVO_ANGLE_MIN;
    if (angle > SERVO_ANGLE_MAX) angle = SERVO_ANGLE_MAX;

    // Linear interpolation between min and max pulse widths
    uint32_t pulse_us = SERVO_PULSE_MIN_US +
        (uint32_t)((uint32_t)(SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US) * (uint32_t)angle
                   / (uint32_t)SERVO_ANGLE_MAX);

    // Duty = pulse_us / period_us * max_duty
    uint32_t max_duty = (1u << SERVO_RESOLUTION) - 1u;   // 16 383
    return (uint32_t)((uint64_t)pulse_us * max_duty / SERVO_PERIOD_US);
}

/** Write duty to one LEDC channel and update it atomically. */
static esp_err_t ledc_write(ledc_channel_t ch, uint32_t duty) {
    esp_err_t r;
    r = ledc_set_duty(SERVO_LEDC_SPEED, ch, duty);
    if (r != ESP_OK) return r;
    return ledc_update_duty(SERVO_LEDC_SPEED, ch);
}

// ─── Init ─────────────────────────────────────────────────────────────────────

esp_err_t peripherals_init(void) {
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Initialising peripherals …");

    // ── 1. Servo LEDC timer ──────────────────────────────────────────────────
    ledc_timer_config_t tim = {
        .speed_mode      = SERVO_LEDC_SPEED,
        .duty_resolution = SERVO_RESOLUTION,
        .timer_num       = SERVO_LEDC_TIMER,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ret = ledc_timer_config(&tim);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "LEDC timer 1 configured  (50 Hz, 14-bit)");

    // ── 2. Pan servo channel ─────────────────────────────────────────────────
    uint32_t center_duty = angle_to_duty(SERVO_ANGLE_CENTER);

    ledc_channel_config_t pan_ch = {
        .gpio_num   = PERIPH_PIN_PAN,
        .speed_mode = SERVO_LEDC_SPEED,
        .channel    = SERVO_LEDC_CH_PAN,
        .timer_sel  = SERVO_LEDC_TIMER,
        .duty       = center_duty,
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&pan_ch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Pan channel config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Pan  servo on GPIO%-2d  duty=%lu (90°)", PERIPH_PIN_PAN, center_duty);

    // ── 3. Tilt servo channel ────────────────────────────────────────────────
    ledc_channel_config_t tilt_ch = {
        .gpio_num   = PERIPH_PIN_TILT,
        .speed_mode = SERVO_LEDC_SPEED,
        .channel    = SERVO_LEDC_CH_TILT,
        .timer_sel  = SERVO_LEDC_TIMER,
        .duty       = center_duty,
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&tilt_ch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Tilt channel config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Tilt servo on GPIO%-2d  duty=%lu (90°)", PERIPH_PIN_TILT, center_duty);

    // ── 4. LED GPIO ──────────────────────────────────────────────────────────
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << PERIPH_PIN_LED),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&led_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    gpio_set_level(PERIPH_PIN_LED, 0);
    ESP_LOGI(TAG, "Flash LED on GPIO%d (off)", PERIPH_PIN_LED);

    // ── 5. Switch / relay GPIO ───────────────────────────────────────────────
    gpio_config_t sw_cfg = {
        .pin_bit_mask = (1ULL << PERIPH_PIN_SWITCH),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&sw_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Switch GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    gpio_set_level(PERIPH_PIN_SWITCH, 0);
    ESP_LOGI(TAG, "Relay switch on GPIO%d (off)", PERIPH_PIN_SWITCH);

    ESP_LOGI(TAG, "All peripherals ready");
    return ESP_OK;
}

// ─── Servo API ────────────────────────────────────────────────────────────────

esp_err_t servo_set_pan(int angle) {
    if (angle < SERVO_ANGLE_MIN) angle = SERVO_ANGLE_MIN;
    if (angle > SERVO_ANGLE_MAX) angle = SERVO_ANGLE_MAX;

    uint32_t duty = angle_to_duty(angle);
    esp_err_t r   = ledc_write(SERVO_LEDC_CH_PAN, duty);
    if (r == ESP_OK) {
        s_pan_angle = angle;
        ESP_LOGD(TAG, "Pan  → %d° (duty=%lu)", angle, duty);
    } else {
        ESP_LOGE(TAG, "Pan  set failed: %s", esp_err_to_name(r));
    }
    return r;
}

esp_err_t servo_set_tilt(int angle) {
    if (angle < SERVO_ANGLE_MIN) angle = SERVO_ANGLE_MIN;
    if (angle > SERVO_ANGLE_MAX) angle = SERVO_ANGLE_MAX;

    uint32_t duty = angle_to_duty(angle);
    esp_err_t r   = ledc_write(SERVO_LEDC_CH_TILT, duty);
    if (r == ESP_OK) {
        s_tilt_angle = angle;
        ESP_LOGD(TAG, "Tilt → %d° (duty=%lu)", angle, duty);
    } else {
        ESP_LOGE(TAG, "Tilt set failed: %s", esp_err_to_name(r));
    }
    return r;
}

int servo_get_pan (void) { return s_pan_angle;  }
int servo_get_tilt(void) { return s_tilt_angle; }

// ─── LED API ──────────────────────────────────────────────────────────────────

esp_err_t led_set(bool on) {
    esp_err_t r = gpio_set_level(PERIPH_PIN_LED, on ? 1 : 0);
    if (r == ESP_OK) {
        s_led_on = on;
        ESP_LOGD(TAG, "LED %s", on ? "ON" : "OFF");
    }
    return r;
}

bool led_get(void) { return s_led_on; }

// ─── Switch / relay API ───────────────────────────────────────────────────────

esp_err_t switch_set(bool on) {
    esp_err_t r = gpio_set_level(PERIPH_PIN_SWITCH, on ? 1 : 0);
    if (r == ESP_OK) {
        s_switch_on = on;
        ESP_LOGD(TAG, "Switch %s", on ? "ON" : "OFF");
    }
    return r;
}

bool switch_get(void) { return s_switch_on; }

#ifndef PERIPHERALS_H
#define PERIPHERALS_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// ─────────────────────────────────────────────
//  GPIO assignments  (free after camera claims)
// ─────────────────────────────────────────────
#define PERIPH_PIN_LED      4    // Onboard flash LED  (active-HIGH)
#define PERIPH_PIN_SWITCH   15   // External relay     (active-HIGH)
#define PERIPH_PIN_PAN      13   // Pan  servo PWM
#define PERIPH_PIN_TILT     14   // Tilt servo PWM

// ─────────────────────────────────────────────
//  LEDC  (camera already uses TIMER_0 / CH_0)
// ─────────────────────────────────────────────
#define SERVO_LEDC_SPEED    LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_TIMER    LEDC_TIMER_1
#define SERVO_LEDC_CH_PAN   LEDC_CHANNEL_1
#define SERVO_LEDC_CH_TILT  LEDC_CHANNEL_2
#define SERVO_FREQ_HZ       50
#define SERVO_RESOLUTION    LEDC_TIMER_14_BIT   // 16 384 ticks per 20 ms

// Standard RC servo pulse widths
#define SERVO_PULSE_MIN_US  1000   // 1 ms  →  0 °
#define SERVO_PULSE_MAX_US  2000   // 2 ms  → 180 °
#define SERVO_PERIOD_US     20000  // 20 ms

// Angle limits
#define SERVO_ANGLE_MIN     0
#define SERVO_ANGLE_MAX     180
#define SERVO_ANGLE_CENTER  90

// ─────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────

/** Initialise all peripherals (call once, before any set_* function). */
esp_err_t peripherals_init(void);

// Servo
esp_err_t servo_set_pan (int angle_deg);
esp_err_t servo_set_tilt(int angle_deg);
int       servo_get_pan (void);
int       servo_get_tilt(void);

// LED
esp_err_t led_set  (bool on);
bool      led_get  (void);

// Relay / switch
esp_err_t switch_set(bool on);
bool      switch_get(void);

#endif // PERIPHERALS_H

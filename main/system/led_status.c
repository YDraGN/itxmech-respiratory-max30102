#include "led_status.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define PIN_RED   ((gpio_num_t)CONFIG_RESPI_PIN_LED_RED)
#define PIN_GREEN ((gpio_num_t)CONFIG_RESPI_PIN_LED_GREEN)
#define TICK_MS   50

#define FAST_HALF_MS      100
#define SLOW_HALF_MS      500
#define CONNECTED_MS      3000
#define ERROR_HALF_MS     200

typedef enum { MODE_NORMAL, MODE_SEARCHING, MODE_CONNECTED, MODE_ERROR } led_mode_t;

/* Ghi từ nhiều task: mỗi biến là một từ 32 bit, đọc/ghi nguyên tử trên C3. */
static volatile led_mode_t s_mode = MODE_NORMAL;
static volatile int64_t s_mode_since_us = 0;
static volatile int64_t s_mode_until_us = 0;   /* 0 = không hết hạn */
static volatile bool s_finger = false;

static void set_led(gpio_num_t pin, bool on) {
#if CONFIG_RESPI_LED_ACTIVE_HIGH
  gpio_set_level(pin, on ? 1 : 0);
#else
  gpio_set_level(pin, on ? 0 : 1);
#endif
}

static void set_mode(led_mode_t mode, uint32_t duration_ms) {
  int64_t now = esp_timer_get_time();
  s_mode_until_us = duration_ms ? now + (int64_t)duration_ms * 1000 : 0;
  s_mode_since_us = now;
  s_mode = mode;
}

static bool blink_phase(int64_t since_us, int half_ms) {
  int64_t elapsed_ms = (esp_timer_get_time() - since_us) / 1000;
  return ((elapsed_ms / half_ms) % 2) == 0;
}

static void led_task(void *arg) {
  (void)arg;
  for (;;) {
    led_mode_t mode = s_mode;
    int64_t until = s_mode_until_us;
    if (until && esp_timer_get_time() >= until && mode != MODE_NORMAL) {
      set_mode(MODE_NORMAL, 0);
      mode = MODE_NORMAL;
    }
    switch (mode) {
      case MODE_SEARCHING: {
        bool on = blink_phase(s_mode_since_us, FAST_HALF_MS);
        set_led(PIN_RED, on);
        set_led(PIN_GREEN, on);
        break;
      }
      case MODE_CONNECTED:
        set_led(PIN_RED, false);
        set_led(PIN_GREEN, blink_phase(s_mode_since_us, SLOW_HALF_MS));
        break;
      case MODE_ERROR:
        set_led(PIN_GREEN, false);
        set_led(PIN_RED, blink_phase(s_mode_since_us, ERROR_HALF_MS));
        break;
      case MODE_NORMAL:
      default:
        set_led(PIN_GREEN, s_finger);
        set_led(PIN_RED, !s_finger);
        break;
    }
    vTaskDelay(pdMS_TO_TICKS(TICK_MS));
  }
}

void led_status_init(void) {
  gpio_reset_pin(PIN_RED);
  gpio_reset_pin(PIN_GREEN);
  gpio_set_direction(PIN_RED, GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_GREEN, GPIO_MODE_OUTPUT);
  set_led(PIN_RED, true);       /* giống bản cũ: đỏ lúc khởi động */
  set_led(PIN_GREEN, false);
  xTaskCreate(led_task, "led", 2048, NULL, 3, NULL);
}

void led_status_searching(uint32_t timeout_s) { set_mode(MODE_SEARCHING, timeout_s * 1000); }
void led_status_connected(void) { set_mode(MODE_CONNECTED, CONNECTED_MS); }
void led_status_normal(void) { set_mode(MODE_NORMAL, 0); }
void led_status_error(void) { set_mode(MODE_ERROR, 0); }
void led_status_set_finger(bool present) { s_finger = present; }

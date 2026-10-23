#include "led_status.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_pattern.h"
#include "sdkconfig.h"

#define PIN_RED   ((gpio_num_t)CONFIG_RESPI_PIN_LED_RED)
#define PIN_GREEN ((gpio_num_t)CONFIG_RESPI_PIN_LED_GREEN)
#define TICK_MS   LED_PULSE_MS

/* Ghi từ nhiều task: mỗi biến là một từ 32 bit, đọc/ghi nguyên tử trên C3. */
static volatile led_link_t s_link = LED_LINK_SEARCHING;
static volatile int64_t s_search_until_us = 0;   /* 0 = không hết hạn */
static volatile bool s_error, s_finger, s_wifi_ready;
static volatile uint8_t s_level;

static void set_led(gpio_num_t pin, bool on) {
#if CONFIG_RESPI_LED_ACTIVE_HIGH
  gpio_set_level(pin, on ? 1 : 0);
#else
  gpio_set_level(pin, on ? 0 : 1);
#endif
}

static void led_task(void *arg) {
  (void)arg;
  for (;;) {
    int64_t now = esp_timer_get_time();
    led_inputs_t in = {
        .error = s_error,
        .link = s_link,
        .level = s_level,
        .worn = s_finger,
        .wifi_ready = s_wifi_ready,
    };
    /* Hết giờ chớp "chờ app": vòng vẫn đang tự lưu, hiện như STANDALONE. */
    if (in.link == LED_LINK_SEARCHING && s_search_until_us && now >= s_search_until_us) in.link = LED_LINK_STANDALONE;
    led_out_t o = led_pattern_eval(&in, (uint32_t)(now / 1000));
    set_led(PIN_RED, o.red);
    set_led(PIN_GREEN, o.green);
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

void led_status_searching(uint32_t timeout_s) {
  s_search_until_us = timeout_s ? esp_timer_get_time() + (int64_t)timeout_s * 1000000 : 0;
  s_link = LED_LINK_SEARCHING;
}
void led_status_gateway(void) { s_link = LED_LINK_GATEWAY; }
void led_status_standalone(void) { s_link = LED_LINK_STANDALONE; }
void led_status_error(void) { s_error = true; }
void led_status_set_finger(bool present) { s_finger = present; }
void led_status_set_level(uint8_t state) { s_level = state; }
void led_status_set_wifi_ready(bool ready) { s_wifi_ready = ready; }

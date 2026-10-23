/* Bảng LED (led_pattern.h): ưu tiên đúng thứ tự và chớp ngắn. */
#include "led_pattern.h"
#include "test_util.h"

/* Số ms LED sáng trong [0, period) bước 10 ms. */
static int on_ms(const led_inputs_t *in, int period, int want_red, int want_green) {
  int on = 0;
  for (int t = 0; t < period; t += 10) {
    led_out_t o = led_pattern_eval(in, (uint32_t)t);
    if (o.red == want_red && o.green == want_green && (o.red || o.green)) on += 10;
  }
  return on;
}

static int lit_ms(const led_inputs_t *in, int period) {
  int on = 0;
  for (int t = 0; t < period; t += 10) {
    led_out_t o = led_pattern_eval(in, (uint32_t)t);
    if (o.red || o.green) on += 10;
  }
  return on;
}

int main(void) {
  led_inputs_t in = {.link = LED_LINK_GATEWAY, .level = 0, .worn = true, .wifi_ready = false};

  CHECK(on_ms(&in, 5000, 0, 1) == LED_PULSE_MS);                /* gateway: xanh 1 lần / 5 s */
  in.link = LED_LINK_STANDALONE;
  CHECK(on_ms(&in, 10000, 0, 1) == LED_PULSE_MS);               /* tự lưu: xanh 1 lần / 10 s */
  in.wifi_ready = true;
  CHECK(on_ms(&in, 5000, 1, 1) == LED_PULSE_MS);                /* WiFi: vàng 1 lần / 5 s */
  in.worn = false;
  CHECK(lit_ms(&in, 10000) == 0);                               /* không đeo: tắt */
  in.link = LED_LINK_SEARCHING;
  CHECK(on_ms(&in, 2000, 1, 1) == 2 * LED_PULSE_MS);            /* chờ app: vàng 2 lần / 2 s, kể cả chưa đeo */
  in.level = 1;
  CHECK(on_ms(&in, 2000, 1, 0) == LED_PULSE_MS);                /* ALERT: đỏ 1 lần / 2 s, hơn mọi trạng thái kết nối */
  in.level = 2;
  CHECK(on_ms(&in, 2000, 1, 0) == 3 * LED_PULSE_MS);            /* CRITICAL: đỏ 3 lần / 2 s */
  in.error = true;
  CHECK(on_ms(&in, 400, 1, 0) == 200);                          /* lỗi cảm biến: đỏ nháy đều */

  led_inputs_t quiet = {.link = LED_LINK_GATEWAY, .worn = true};
  CHECK(lit_ms(&quiet, 5000) * 100 / 5000 <= 1);                /* sáng ≤ 1% thời gian: tiết kiệm pin */
  return test_report("led_pattern");
}

#include "led_pattern.h"

#define PULSE_GAP_MS 200   /* khoảng giữa hai lần chớp liên tiếp trong một nhóm */

/* true trong count lần chớp LED_PULSE_MS ở đầu mỗi chu kỳ period_ms. */
static bool pulse(uint32_t t_ms, uint32_t period_ms, uint32_t count) {
  uint32_t phase = t_ms % period_ms;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t start = i * PULSE_GAP_MS;
    if (phase >= start && phase < start + LED_PULSE_MS) return true;
  }
  return false;
}

led_out_t led_pattern_eval(const led_inputs_t *in, uint32_t t_ms) {
  led_out_t o = {false, false};
  if (in->error) {
    o.red = (t_ms % 400) < 200;
  } else if (in->level == 2) {
    o.red = pulse(t_ms, 2000, 3);
  } else if (in->level == 1) {
    o.red = pulse(t_ms, 2000, 1);
  } else if (in->link == LED_LINK_SEARCHING) {
    o.red = o.green = pulse(t_ms, 2000, 2);
  } else if (!in->worn) {
    /* tắt */
  } else if (in->link == LED_LINK_STANDALONE) {
    if (in->wifi_ready) o.red = o.green = pulse(t_ms, 5000, 1);
    else o.green = pulse(t_ms, 10000, 1);
  } else {
    o.green = pulse(t_ms, 5000, 1);
  }
  return o;
}

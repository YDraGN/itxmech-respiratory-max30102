#include "device_clock.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "clock";

/* epoch = uptime + offset. offset = 0 nghĩa là chưa đồng bộ. Ghi/đọc một từ 32 bit là nguyên tử trên C3. */
static volatile uint32_t s_offset = 0;

static uint32_t uptime_s(void) { return (uint32_t)(esp_timer_get_time() / 1000000); }

bool device_clock_set(uint32_t epoch) {
  if (epoch < CLOCK_MIN_VALID_EPOCH) {
    ESP_LOGW(TAG, "bỏ qua giờ không hợp lệ %lu", (unsigned long)epoch);
    return false;
  }
  uint32_t old = s_offset;
  s_offset = epoch - uptime_s();
  if (old == 0) ESP_LOGI(TAG, "đã đồng bộ giờ: %lu", (unsigned long)epoch);
  else ESP_LOGI(TAG, "chỉnh giờ lệch %ld s", (long)(s_offset - old));
  return true;
}

bool device_clock_is_synced(void) { return s_offset != 0; }

uint32_t device_clock_now(void) { return uptime_s() + s_offset; }

uint32_t device_clock_fix_ts(uint32_t ts) {
  if (ts >= CLOCK_MIN_VALID_EPOCH || s_offset == 0) return ts;
  return ts + s_offset;
}

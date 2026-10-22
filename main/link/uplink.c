#include "uplink.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "record_store.h"
#include "wristband_config.h"

static const char *TAG = "uplink";

#define NOTIFY_KICK    (1u << 0)
#define NOTIFY_URGENT  (1u << 1)

static TaskHandle_t s_task;
static volatile bool s_active;

/* Chỗ sẽ gắn WiFi + HTTPS. Trả true khi server đã nhận (acked_seq tăng). */
static bool uplink_flush(bool urgent) {
  ESP_LOGI(TAG, "%s: %u bản ghi chờ gửi (acked %lu / last %lu) - chưa cấu hình server, giữ trong flash",
           urgent ? "cảnh báo" : "định kỳ", record_store_backlog(),
           (unsigned long)record_store_acked_seq(), (unsigned long)record_store_last_seq());
  return false;
}

static void uplink_task(void *arg) {
  (void)arg;
  for (;;) {
    wb_config_t cfg;
    wb_config_get(&cfg);
    TickType_t wait = s_active ? pdMS_TO_TICKS((uint32_t)cfg.upload_interval_min * 60u * 1000u) : portMAX_DELAY;
    uint32_t bits = 0;
    bool woke = xTaskNotifyWait(0, UINT32_MAX, &bits, wait) == pdTRUE;
    if (!s_active) continue;
    if (woke && !(bits & (NOTIFY_KICK | NOTIFY_URGENT))) continue;
    uplink_flush((bits & NOTIFY_URGENT) != 0);
  }
}

void uplink_init(void) {
  xTaskCreate(uplink_task, "uplink", 4096, NULL, 4, &s_task);
}

void uplink_set_active(bool active) {
  bool was = s_active;
  s_active = active;
  if (active && !was) uplink_kick(false);   /* vào STANDALONE: thử gửi ngay một lần */
}

void uplink_kick(bool urgent) {
  if (s_task) xTaskNotify(s_task, urgent ? NOTIFY_URGENT : NOTIFY_KICK, eSetBits);
}

bool uplink_wifi_on(void) { return false; }

#include "wristband_config.h"

#include <string.h>
#include "device_state.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "config";

static wb_config_t s_cfg;
static SemaphoreHandle_t s_mutex;
static QueueHandle_t s_pending;   /* 1 phần tử, xQueueOverwrite: chỉ giữ config mới nhất */

void wb_config_init(void) {
  s_mutex = xSemaphoreCreateMutex();
  s_pending = xQueueCreate(1, sizeof(wb_config_t));
  if (device_state_load_config(&s_cfg, sizeof(s_cfg)) &&
      spo2tc_config_set_T(&s_cfg.eng, s_cfg.eng.T) == 0) {
    ESP_LOGI(TAG, "config v%u, T=%d, ML %s", s_cfg.version, s_cfg.eng.T,
             (s_cfg.ml_enabled && s_cfg.model_ok) ? "bật" : "tắt");
    /* Áp vào engine ở phút đầu tiên, giống config nhận lúc chạy. */
    xQueueOverwrite(s_pending, &s_cfg);
  } else {
    wb_config_set_default(&s_cfg);
    ESP_LOGI(TAG, "dùng config xuất xưởng (v0, T=%d)", s_cfg.eng.T);
  }
}

void wb_config_get(wb_config_t *out) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  *out = s_cfg;
  xSemaphoreGive(s_mutex);
}

bool wb_config_accept(const wb_config_t *c) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  bool newer = c->version > s_cfg.version;
  if (newer) {
    device_state_save_config(c, sizeof(*c));   /* lưu trước, mất điện vẫn còn bản đầy đủ */
    s_cfg = *c;
  }
  xSemaphoreGive(s_mutex);
  if (newer) {
    xQueueOverwrite(s_pending, c);
    ESP_LOGI(TAG, "nhận config v%u (T=%d, ML %s)", c->version, c->eng.T,
             (c->ml_enabled && c->model_ok) ? "bật" : "chỉ luật");
  }
  return newer;
}

bool wb_config_take_pending(wb_config_t *out) {
  return xQueueReceive(s_pending, out, 0) == pdTRUE;
}

#include "device_cred.h"

#include <string.h>

#include "device_state.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "device_cred";

static device_cred_t s_cred;
static bool s_has;
static SemaphoreHandle_t s_mutex;

static bool valid(const device_cred_t *c) {
  size_t k = strnlen(c->key, sizeof(c->key)), u = strnlen(c->url, sizeof(c->url));
  return k >= 1 && k <= DEVICE_KEY_MAX_LEN && u >= 8 && u <= SERVER_URL_MAX_LEN;
}

void device_cred_init(void) {
  s_mutex = xSemaphoreCreateMutex();
  s_has = device_state_load_blob("dcred", &s_cred, sizeof(s_cred)) && valid(&s_cred);
  if (!s_has) memset(&s_cred, 0, sizeof(s_cred));
  /* Không log khóa. */
  if (s_has) ESP_LOGI(TAG, "có khóa thiết bị, server %s", s_cred.url);
  else ESP_LOGI(TAG, "chưa có khóa thiết bị (app cấp sau khi cài WiFi)");
}

bool device_cred_get(device_cred_t *out) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  bool has = s_has;
  if (has) *out = s_cred;
  xSemaphoreGive(s_mutex);
  return has;
}

bool device_cred_set(const char *key, const char *url) {
  device_cred_t c;
  memset(&c, 0, sizeof(c));
  strncpy(c.key, key, DEVICE_KEY_MAX_LEN);
  strncpy(c.url, url, SERVER_URL_MAX_LEN);
  if (!valid(&c)) return false;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  bool ok = device_state_save_blob("dcred", &c, sizeof(c)) == ESP_OK;
  if (ok) {
    s_cred = c;
    s_has = true;
  }
  xSemaphoreGive(s_mutex);
  if (ok) ESP_LOGI(TAG, "đã lưu khóa thiết bị mới, server %s", c.url);
  return ok;
}

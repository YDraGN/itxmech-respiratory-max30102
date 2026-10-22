#include "wifi_store.h"

#include "device_state.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "wifi_store";

static wifi_list_t s_list;
static SemaphoreHandle_t s_mutex;

void wifi_store_init(void) {
  s_mutex = xSemaphoreCreateMutex();
  if (!device_state_load_blob("wifi", &s_list, sizeof(s_list)) || !wifi_list_valid(&s_list)) wifi_list_clear(&s_list);
  ESP_LOGI(TAG, "%u mạng WiFi đã lưu", s_list.count);
}

void wifi_store_get(wifi_list_t *out) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  *out = s_list;
  xSemaphoreGive(s_mutex);
}

bool wifi_store_put(const char *ssid, const char *pass) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  bool ok = wifi_list_put(&s_list, ssid, pass);
  if (ok) device_state_save_blob("wifi", &s_list, sizeof(s_list));
  uint8_t count = s_list.count;
  xSemaphoreGive(s_mutex);
  /* Không log mật khẩu. */
  if (ok) ESP_LOGI(TAG, "đã lưu WiFi \"%s\" (%u mạng)", ssid, count);
  return ok;
}

esp_err_t wifi_store_clear(void) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  wifi_list_clear(&s_list);
  esp_err_t err = device_state_save_blob("wifi", &s_list, sizeof(s_list));
  xSemaphoreGive(s_mutex);
  ESP_LOGI(TAG, "đã xóa danh sách WiFi");
  return err;
}

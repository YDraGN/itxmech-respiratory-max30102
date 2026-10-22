#include "device_state.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "state";
static const char *NS = "respi";
#define CFG_MAGIC 0x31474643u   /* "CFG1" */
#define BLOB_MAX  320

static nvs_handle_t s_nvs;
static uint16_t s_boot_id;
static char s_device_id[24];

static void make_device_id(void) {
  if (strlen(CONFIG_RESPI_DEVICE_ID) > 0) {
    snprintf(s_device_id, sizeof(s_device_id), "%s", CONFIG_RESPI_DEVICE_ID);
    return;
  }
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(s_device_id, sizeof(s_device_id), "WB-C3-%02X%02X", mac[4], mac[5]);
}

esp_err_t device_state_init(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS hỏng hoặc khác phiên bản, xóa và tạo lại");
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  if (err != ESP_OK) return err;
  err = nvs_open(NS, NVS_READWRITE, &s_nvs);
  if (err != ESP_OK) return err;

  s_boot_id = (uint16_t)(device_state_get_u32("boot_id", 0) + 1);
  device_state_set_u32("boot_id", s_boot_id);
  make_device_id();
  ESP_LOGI(TAG, "device_id=%s boot_id=%u", s_device_id, s_boot_id);
  return ESP_OK;
}

uint16_t device_state_boot_id(void) { return s_boot_id; }

const char *device_state_device_id(void) { return s_device_id; }

uint32_t device_state_get_u32(const char *key, uint32_t fallback) {
  uint32_t v = fallback;
  if (nvs_get_u32(s_nvs, key, &v) != ESP_OK) return fallback;
  return v;
}

esp_err_t device_state_set_u32(const char *key, uint32_t value) {
  esp_err_t err = nvs_set_u32(s_nvs, key, value);
  if (err == ESP_OK) err = nvs_commit(s_nvs);
  if (err != ESP_OK) ESP_LOGW(TAG, "không ghi được %s: %s", key, esp_err_to_name(err));
  return err;
}

/* Blob: [magic u32][size u32][dữ liệu]. Đổi cấu trúc thì size khác, bản cũ bị bỏ qua. */
bool device_state_load_blob(const char *key, void *data, size_t size) {
  uint8_t buf[8 + BLOB_MAX];
  if (size > BLOB_MAX) return false;
  size_t len = sizeof(buf);
  if (nvs_get_blob(s_nvs, key, buf, &len) != ESP_OK || len != 8 + size) return false;
  uint32_t magic, stored;
  memcpy(&magic, buf, 4);
  memcpy(&stored, buf + 4, 4);
  if (magic != CFG_MAGIC || stored != size) return false;
  memcpy(data, buf + 8, size);
  return true;
}

esp_err_t device_state_save_blob(const char *key, const void *data, size_t size) {
  uint8_t buf[8 + BLOB_MAX];
  if (size > BLOB_MAX) return ESP_ERR_INVALID_SIZE;
  uint32_t magic = CFG_MAGIC, sz = (uint32_t)size;
  memcpy(buf, &magic, 4);
  memcpy(buf + 4, &sz, 4);
  memcpy(buf + 8, data, size);
  esp_err_t err = nvs_set_blob(s_nvs, key, buf, 8 + size);
  if (err == ESP_OK) err = nvs_commit(s_nvs);
  if (err != ESP_OK) ESP_LOGE(TAG, "không lưu được %s: %s", key, esp_err_to_name(err));
  return err;
}

bool device_state_load_config(void *cfg, size_t size) { return device_state_load_blob("cfg", cfg, size); }

esp_err_t device_state_save_config(const void *cfg, size_t size) { return device_state_save_blob("cfg", cfg, size); }

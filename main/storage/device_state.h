#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Trạng thái nhỏ cần giữ qua lần khởi động lại, lưu trong NVS (namespace "respi"). */

/* Khởi tạo NVS và tăng boot_id. Gọi đầu tiên trong app_main. */
esp_err_t device_state_init(void);

uint16_t device_state_boot_id(void);

/* device_id: CONFIG_RESPI_DEVICE_ID, nếu trống thì WB-C3-XXXX theo MAC Bluetooth. */
const char *device_state_device_id(void);

uint32_t device_state_get_u32(const char *key, uint32_t fallback);
esp_err_t device_state_set_u32(const char *key, uint32_t value);

/* Blob có kiểm tra kích thước (tối đa 320 B). */
bool device_state_load_blob(const char *key, void *data, size_t size);
esp_err_t device_state_save_blob(const char *key, const void *data, size_t size);

bool device_state_load_config(void *cfg, size_t size);
esp_err_t device_state_save_config(const void *cfg, size_t size);

#ifdef __cplusplus
}
#endif

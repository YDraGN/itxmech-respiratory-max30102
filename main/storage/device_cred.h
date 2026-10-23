#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Khóa thiết bị + địa chỉ server cho chế độ WiFi. App xin khóa (POST /api/app/v1/device-key) rồi ghi xuống
 * qua BLE (WIFI_PROV op 0x03). Vòng gửi khóa trong header `x-device-key` khi tự POST lên các endpoint ingest.
 * Phần kiểu dữ liệu là C thuần (test trên máy tính); lưu NVS ở device_cred.c.
 */

#define DEVICE_KEY_MAX_LEN  64
#define SERVER_URL_MAX_LEN  128   /* gốc API, không có "/" cuối, ví dụ https://host/api/app/v1 */

typedef struct {
  char key[DEVICE_KEY_MAX_LEN + 1];
  char url[SERVER_URL_MAX_LEN + 1];
} device_cred_t;

void device_cred_init(void);

/* false nếu chưa có khóa (app chưa cấp). */
bool device_cred_get(device_cred_t *out);
bool device_cred_set(const char *key, const char *url);

#ifdef __cplusplus
}
#endif

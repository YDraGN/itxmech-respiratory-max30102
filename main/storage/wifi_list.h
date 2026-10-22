#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Danh sách WiFi app gửi xuống qua BLE (characteristic WIFI_PROV), dùng cho uplink khi không có app.
 * C thuần để test trên máy tính; lưu NVS ở wifi_store.c.
 */

#define WIFI_LIST_MAX      3
#define WIFI_SSID_MAX_LEN  32
#define WIFI_PASS_MAX_LEN  63
#define WIFI_PASS_MIN_LEN  8    /* WPA2; mật khẩu rỗng = mạng mở */

typedef struct {
  char ssid[WIFI_SSID_MAX_LEN + 1];
  char pass[WIFI_PASS_MAX_LEN + 1];
} wifi_network_t;

typedef struct {
  uint8_t count;
  wifi_network_t net[WIFI_LIST_MAX];   /* [0] cũ nhất */
} wifi_list_t;

void wifi_list_clear(wifi_list_t *l);

/* Thêm hoặc thay mật khẩu cho SSID đã có. Đầy thì bỏ mạng cũ nhất. false nếu độ dài không hợp lệ. */
bool wifi_list_put(wifi_list_t *l, const char *ssid, const char *pass);

bool wifi_list_valid(const wifi_list_t *l);

#ifdef __cplusplus
}
#endif

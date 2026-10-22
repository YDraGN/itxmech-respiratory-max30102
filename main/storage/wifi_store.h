#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "wifi_list.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Danh sách WiFi đã lưu (NVS, khóa "wifi"). An toàn giữa các task. */
void wifi_store_init(void);
void wifi_store_get(wifi_list_t *out);
/* false nếu SSID/mật khẩu sai độ dài. */
bool wifi_store_put(const char *ssid, const char *pass);
esp_err_t wifi_store_clear(void);

#ifdef __cplusplus
}
#endif

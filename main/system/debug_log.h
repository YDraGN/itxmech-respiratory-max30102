#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Nhật ký qua BLE (characteristic DEBUG_LOG a51c000b), chỉ có khi bật CONFIG_RESPI_BLE_DEBUG_LOG.
 * Mọi dòng ESP_LOG vẫn ra cổng USB như cũ; khi có máy bật notify DEBUG_LOG thì được chép thêm vào
 * một bộ đệm 4 KB để task ble_log gửi đi. Không ai nghe thì không chép gì (không tốn CPU, không tốn sóng).
 */

void debug_log_init(void);
void debug_log_set_enabled(bool on);
/* Chờ tối đa wait_ms để lấy một đoạn log (tối đa max byte). Trả số byte, 0 nếu hết giờ. */
size_t debug_log_read(char *out, size_t max, unsigned wait_ms);

#ifdef __cplusplus
}
#endif

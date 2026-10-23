#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hai LED ngoài (đỏ + xanh). Chỉ task LED được ghi GPIO, các module khác chỉ báo trạng thái;
 * bảng nhịp chớp ở led_pattern.h.
 */
void led_status_init(void);

/* Đang chờ app. timeout_s = 0: chớp đến khi có app; > 0: hết giờ thì chuyển sang nhịp "tự lưu". */
void led_status_searching(uint32_t timeout_s);
void led_status_gateway(void);
void led_status_standalone(void);
void led_status_error(void);

/* Task cảm biến: có tay / đang đeo. */
void led_status_set_finger(bool present);
/* Task phút: mức hiện tại (ble_vital_state_t). */
void led_status_set_level(uint8_t state);
/* Uplink: đã có WiFi + khóa thiết bị (chế độ WiFi dùng được). */
void led_status_set_wifi_ready(bool ready);

#ifdef __cplusplus
}
#endif

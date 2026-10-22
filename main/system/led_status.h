#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hai LED ngoài (đỏ + xanh). Chỉ task LED được ghi GPIO, các module khác chỉ đổi chế độ:
 *   SEARCHING  cả hai LED nháy nhanh (100 ms) - đang quảng bá BLE, chờ app
 *   CONNECTED  LED xanh nháy chậm (500 ms) trong 3 s - app vừa bắt tay xong
 *   NORMAL     như bản phần cứng cũ: xanh = có tay/đang đeo, đỏ = không có tay
 *   ERROR      LED đỏ nháy 200 ms - không tìm thấy MAX30102
 */
void led_status_init(void);

/* timeout_s = 0: nháy đến khi có chế độ khác; > 0: hết giờ thì về NORMAL. */
void led_status_searching(uint32_t timeout_s);
void led_status_connected(void);
void led_status_normal(void);
void led_status_error(void);

/* Gọi từ task cảm biến, dùng cho chế độ NORMAL. */
void led_status_set_finger(bool present);

#ifdef __cplusplus
}
#endif

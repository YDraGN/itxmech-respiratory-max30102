#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bảng LED (hai LED đỏ + xanh; "vàng" = bật cả hai). Chớp ngắn 50 ms để tiết kiệm pin.
 * Ưu tiên từ trên xuống, dòng đầu tiên khớp quyết định:
 *
 *   Lỗi cảm biến (không thấy MAX30102)     đỏ nháy đều 200 ms
 *   Mức CRITICAL                            đỏ chớp 3 lần, mỗi 2 s
 *   Mức ALERT                               đỏ chớp 1 lần, mỗi 2 s
 *   Đang chờ app (quảng bá BLE)             xanh + đỏ chớp 2 lần, mỗi 2 s
 *   Không đeo                               tắt
 *   Chế độ WiFi (tự gửi lên server)         vàng chớp 1 lần, mỗi 5 s
 *   Tự lưu, chưa cài WiFi / chưa có khóa    xanh chớp 1 lần, mỗi 10 s
 *   Đã nối app (gateway), bình thường       xanh chớp 1 lần, mỗi 5 s
 *
 * C thuần để test trên máy tính; task LED ở led_status.c gọi mỗi 50 ms.
 */

typedef enum { LED_LINK_SEARCHING, LED_LINK_GATEWAY, LED_LINK_STANDALONE } led_link_t;

typedef struct {
  bool error;
  led_link_t link;
  uint8_t level;       /* ble_vital_state_t: 1 = ALERT, 2 = CRITICAL, còn lại không báo */
  bool worn;
  bool wifi_ready;     /* có ít nhất một WiFi và khóa thiết bị */
} led_inputs_t;

typedef struct {
  bool red, green;
} led_out_t;

#define LED_PULSE_MS 50

/* t_ms: thời gian chạy (ms), dùng chung một đồng hồ để các nhịp đều nhau. */
led_out_t led_pattern_eval(const led_inputs_t *in, uint32_t t_ms);

#ifdef __cplusplus
}
#endif

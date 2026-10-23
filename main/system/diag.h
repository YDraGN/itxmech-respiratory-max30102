#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ble_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Số liệu hiệu năng để đo khi vòng chạy không cắm USB (ble-gatt.md mục 3.9):
 * - % CPU mỗi giây = 100 − phần thời gian của task IDLE (FreeRTOS run-time stats), lấy trung bình và đỉnh;
 * - thời gian lâu nhất của: một lần chạy model, xử lý một phút, một lần ghi flash;
 * - RAM trống thấp nhất, số giây bật WiFi, số gói BLE bị bỏ, lý do reset.
 * Mỗi RESPI_DIAG_PERIOD_MIN phút đóng một cửa sổ thành ble_diag_t, giữ trong vòng đệm RAM (DIAG_RING mục)
 * cho tới khi gửi được: qua BLE (app chuyển lên server) hoặc qua WiFi (ingest/diagnostics).
 */

#define DIAG_RING 24   /* 2 giờ với cửa sổ 5 phút */

void diag_init(void);

/* Gọi mỗi giây (task cảm biến): đo % CPU của giây vừa qua. */
void diag_tick_second(void);

/* Thời gian đo bằng esp_timer_get_time(), đơn vị µs. An toàn giữa các task. */
void diag_note_model_us(int64_t us);
void diag_note_minute_us(int64_t us);
void diag_note_flash_us(int64_t us);
void diag_note_wifi_ms(uint32_t ms);
void diag_note_ble_drop(void);

/* Đóng cửa sổ hiện tại (task phút), đưa vào vòng đệm, in một dòng log tóm tắt. */
void diag_finish_window(ble_diag_t *out);

/* Chép tối đa max mục chưa gửi (cũ trước), trả số mục. diag_consume(n) bỏ n mục đầu sau khi gửi xong. */
size_t diag_pending(ble_diag_t *out, size_t max);
void diag_consume(size_t n);

uint8_t diag_reset_reason(void);

/* Cửa sổ vừa đóng gần nhất (đọc DIAG qua BLE). false nếu chưa đóng cửa sổ nào. */
bool diag_latest(ble_diag_t *out);

#ifdef __cplusplus
}
#endif

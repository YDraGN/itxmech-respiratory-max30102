#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ble_proto.h"
#include "esp_err.h"
#include "record_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Kho bản ghi dùng chung cho BLE và uplink sau này: bọc record_log bằng partition "reclog",
 * khóa giữa các task, cấp seq (không reset khi khởi động lại) và giữ acked_seq trong NVS.
 *
 * ACK chỉ dời con trỏ acked_seq, không xóa flash. Bản ghi cũ chỉ mất khi vòng đệm ghi đè.
 * Nhờ vậy điện thoại khác (hoặc uplink WiFi) vẫn xin lại được dữ liệu điện thoại trước đã ACK
 * nhưng có thể chưa kịp gửi lên server.
 */

esp_err_t record_store_init(void);

/* Gán seq mới cho rec rồi ghi flash. Trả seq (vẫn cấp seq kể cả khi ghi flash lỗi, có log). */
uint32_t record_store_add_telemetry(ble_record_t *rec);
esp_err_t record_store_add_assessment(const ble_assessment_t *a);

uint32_t record_store_last_seq(void);
uint32_t record_store_acked_seq(void);
uint16_t record_store_backlog(void);

/* true nếu acked_seq tăng. seq lớn hơn last_seq bị cắt về last_seq. */
bool record_store_ack(uint32_t seq);

/* seq bắt đầu thực tế cho BACKFILL(from), có giới hạn CONFIG_RESPI_BACKFILL_KEEP_ACKED. */
uint32_t record_store_backfill_start(uint32_t requested_from);

/* Duyệt từ cũ đến mới, mỗi lần next giữ khóa rất ngắn. */
void record_store_cursor_init(rlog_cursor_t *c);
bool record_store_cursor_next(rlog_cursor_t *c, uint32_t from_seq, uint32_t type_mask, rlog_entry_t *out);

#ifdef __cplusplus
}
#endif

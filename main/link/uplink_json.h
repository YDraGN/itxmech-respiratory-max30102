#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ble_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * JSON cho đường WiFi (vòng tự gửi lên /api/app/v1/ingest/...), cùng định dạng app gửi khi chuyển tiếp BLE
 * (OutboxUploader.kt) để server xử lý như nhau. C thuần, không phụ thuộc ESP-IDF; tests/host kiểm tra.
 */

typedef struct {
  const char *device_id;
  const char *fw;
  const char *model_version;
  uint16_t boot_id;
  uint16_t config_version;
} uplink_meta_t;

/* Mỗi lô vừa bộ đệm 10 KB của uplink (server nhận tối đa 120 / 50). */
#define UPLINK_TELEMETRY_BATCH  60
#define UPLINK_ASSESSMENT_BATCH 20
#define UPLINK_DIAG_BATCH       24   /* cả vòng đệm DIAG (2 giờ) trong một request */

/* Trả độ dài chuỗi (không tính byte 0), 0 nếu buf không đủ chỗ. */
size_t uplink_json_telemetry(char *buf, size_t cap, const uplink_meta_t *m, const ble_record_t *r, size_t n);
size_t uplink_json_assessments(char *buf, size_t cap, const uplink_meta_t *m, const ble_assessment_t *a, size_t n);
/* Số liệu hiệu năng (DIAG) gửi lên ingest/diagnostics, cùng định dạng app chuyển tiếp qua BLE. */
size_t uplink_json_diagnostics(char *buf, size_t cap, const uplink_meta_t *m, const ble_diag_t *d, size_t n);
size_t uplink_json_config_ack(char *buf, size_t cap, const char *device_id, uint16_t version, const char *status,
                              uint32_t applied_at);

/*
 * Đọc trường số / chuỗi ở phản hồi của server (đối tượng phẳng, ví dụ acked_seq, server_time,
 * config_version_latest, ble_config_hex). Không phải bộ phân tích JSON đầy đủ: tìm `"key"` rồi đọc giá trị sau dấu ":".
 */
bool uplink_json_get_int(const char *body, const char *key, long long *out);
bool uplink_json_get_str(const char *body, const char *key, char *out, size_t cap);

/* "0958…" → n byte. false nếu độ dài khác 2n hoặc có ký tự không phải hex. */
bool uplink_hex_decode(const char *hex, uint8_t *out, size_t n);

#ifdef __cplusplus
}
#endif

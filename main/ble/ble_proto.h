#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "spo2_tc.h"
#include "wifi_list.h"
#include "wristband_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mã hóa / giải mã gói BLE theo đặc tả GATT v1 (itxmech-respiratory-mobile-app/docs/ble-gatt.md).
 * C thuần, không phụ thuộc ESP-IDF; tests/host kiểm tra khớp từng byte với app.
 * Little-endian. Giá trị thiếu: u16 ×10 = 0xFFFF, u8 = 0xFF. Trong C dùng NAN cho "thiếu".
 */

#define BLE_PROTO_SPEC_VERSION      1
#define BLE_PROTO_LIVE_SIZE         8
#define BLE_PROTO_RECORD_SIZE       16
#define BLE_PROTO_ASSESSMENT_SIZE   24
#define BLE_PROTO_STATUS_SIZE       14
#define BLE_PROTO_CONTROL_SIZE      5
#define BLE_PROTO_CONFIG_SIZE       16
#define BLE_PROTO_MAX_RECORDS       15
#define BLE_PROTO_TS_OFFSET         4    /* vị trí ts trong RECORD và ASSESSMENT */
#define BLE_PROTO_BATTERY_UNKNOWN   0xFF

typedef enum {
  BLE_OP_ACK = 0x01,
  BLE_OP_TIME_SYNC = 0x02,
  BLE_OP_BACKFILL = 0x03,
  BLE_OP_SET_GATEWAY = 0x04,
} ble_control_op_t;

/* Lỗi ATT trả cho app khi CONFIG / CONTROL không hợp lệ. */
#define BLE_PROTO_ATT_ERR_INVALID 0x80

typedef struct {
  uint32_t ts;
  float spo2;        /* % */
  float pulse;       /* bpm */
  uint8_t sqi_pct;   /* 0..100 */
} ble_live_t;

typedef struct {
  uint32_t seq;
  uint32_t ts;              /* đầu phút */
  float spo2_mean, spo2_min;
  uint8_t spo2_valid_pct;
  float pulse_mean;
  uint8_t sqi_pct;
  bool worn;
} ble_record_t;

typedef enum {
  BLE_STATE_NORMAL = 0, BLE_STATE_ALERT = 1, BLE_STATE_CRITICAL = 2, BLE_STATE_UNKNOWN = 3, BLE_STATE_WATCH = 4,
} ble_vital_state_t;

typedef enum {
  BLE_TRIG_NONE = 0, BLE_TRIG_RULE_CRITICAL = 1, BLE_TRIG_RULE_ALERT = 2, BLE_TRIG_ML_RISK = 3,
  BLE_TRIG_HR_OUT_OF_RANGE = 4, BLE_TRIG_SIGNAL_LOST = 5,
} ble_trigger_t;

/* Thứ tự bit của reasons (mục 3.4). */
enum {
  BLE_REASON_SPO2_DOWNTREND = 1 << 0,
  BLE_REASON_NEAR_THRESHOLD = 1 << 1,
  BLE_REASON_BELOW_ALERT = 1 << 2,
  BLE_REASON_BELOW_CRITICAL = 1 << 3,
  BLE_REASON_HR_HIGH = 1 << 4,
  BLE_REASON_HR_LOW = 1 << 5,
  BLE_REASON_LOW_SIGNAL = 1 << 6,
  BLE_REASON_NOT_WORN = 1 << 7,
  BLE_REASON_NO_DATA = 1 << 8,
};

typedef struct {
  uint32_t seq, ts;
  uint8_t state;            /* ble_vital_state_t */
  uint8_t trigger;          /* ble_trigger_t */
  int8_t out_q;             /* -128 = không chạy model */
  uint8_t T;
  float spo2_now, spo2_min_10m;
  float spo2_slope_30m;     /* %/phút */
  float pulse_now;
  bool local_notified;
  uint16_t reasons;
  uint16_t config_version;
} ble_assessment_t;

typedef struct {
  uint8_t battery_pct;      /* BLE_PROTO_BATTERY_UNKNOWN nếu chưa đo được */
  bool worn, charging, on_wifi;
  uint16_t backlog;
  uint32_t acked_seq, last_seq;
  uint16_t config_version;
} ble_status_t;

void ble_proto_encode_live(const ble_live_t *v, uint8_t out[BLE_PROTO_LIVE_SIZE]);
void ble_proto_encode_record(const ble_record_t *r, uint8_t out[BLE_PROTO_RECORD_SIZE]);
void ble_proto_encode_assessment(const ble_assessment_t *a, uint8_t out[BLE_PROTO_ASSESSMENT_SIZE]);
void ble_proto_encode_status(const ble_status_t *s, uint8_t out[BLE_PROTO_STATUS_SIZE]);

uint32_t ble_proto_get_u32(const uint8_t *p);
void ble_proto_put_u32(uint8_t *p, uint32_t v);

/* false nếu sai độ dài. */
bool ble_proto_decode_control(const uint8_t *in, size_t len, uint8_t *op, uint32_t *arg);

typedef enum {
  BLE_CFG_OK = 0,
  BLE_CFG_ERR_SIZE,
  BLE_CFG_ERR_T_OUT_OF_RANGE,
  BLE_CFG_ERR_FIELD,
} ble_cfg_err_t;

/*
 * Giải mã CONFIG 16 B lên trên config hiện tại (giữ crit_cooldown vì gói không mang).
 * Kiểm tra theo CONFIG_VA_API.md §3.2. Ngưỡng INT8 lệch bảng firmware → model_ok = false.
 * Không xét version ở đây; bên gọi so với version đang dùng.
 */
ble_cfg_err_t ble_proto_decode_config(const uint8_t *in, size_t len, const wb_config_t *current, wb_config_t *out);
void ble_proto_encode_config(const wb_config_t *c, uint8_t out[BLE_PROTO_CONFIG_SIZE]);

/*
 * WIFI_PROV (a51c0009, read + write có phản hồi), phần mở rộng của đặc tả v1:
 *   ghi 0x01 | ssid_len u8 | ssid | pass_len u8 | pass   thêm hoặc thay mật khẩu (pass_len 0 = mạng mở)
 *   ghi 0x02                                              xóa hết
 *   đọc  count u8 | (ssid_len u8 | ssid) × count          không bao giờ trả mật khẩu
 */
typedef enum { BLE_WIFI_OP_PUT = 0x01, BLE_WIFI_OP_CLEAR = 0x02 } ble_wifi_op_t;

typedef struct {
  uint8_t op;
  char ssid[WIFI_SSID_MAX_LEN + 1];
  char pass[WIFI_PASS_MAX_LEN + 1];
} ble_wifi_cmd_t;

#define BLE_PROTO_WIFI_LIST_MAX_SIZE (1 + WIFI_LIST_MAX * (1 + WIFI_SSID_MAX_LEN))

/* false nếu sai định dạng hoặc độ dài (SSID 1–32, mật khẩu 0 hoặc 8–63). */
bool ble_proto_decode_wifi(const uint8_t *in, size_t len, ble_wifi_cmd_t *out);
size_t ble_proto_encode_wifi_list(const wifi_list_t *l, uint8_t out[BLE_PROTO_WIFI_LIST_MAX_SIZE]);

ble_vital_state_t ble_proto_state_from_level(spo2tc_level_t level);
ble_trigger_t ble_proto_trigger_from_result(const spo2tc_result_t *r);

#ifdef __cplusplus
}
#endif

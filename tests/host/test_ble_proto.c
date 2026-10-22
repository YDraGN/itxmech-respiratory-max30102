/* Kiểm tra mã hóa BLE khớp từng byte với đặc tả (và BlePacketCodec.kt của app). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ble_proto.h"
#include "test_util.h"

static void test_config_example_from_spec(void) {
  /* Ví dụ ở ble-gatt.md 3.7 / CONFIG_VA_API.md 3.3: T = 88, version 9. */
  const uint8_t bytes[16] = {0x09, 0x00, 0x58, 0x55, 0x03, 0x32, 0x2D, 0x78,
                             0x01, 0x47, 0x56, 0x01, 0x01, 0x3C, 0x0A, 0x1E};
  wb_config_t cur, out;
  wb_config_set_default(&cur);
  CHECK(ble_proto_decode_config(bytes, sizeof(bytes), &cur, &out) == BLE_CFG_OK);
  CHECK(out.version == 9);
  CHECK(out.eng.T == 88);
  CHECK(out.eng.crit_T == 85 && out.eng.crit_n_min == 3);
  CHECK(fabsf(out.eng.crit_sqi - 0.5f) < 1e-6f);
  CHECK(out.eng.hr_low == 45 && out.eng.hr_high == 120);
  CHECK(out.ml_enabled && out.model_ok);
  CHECK(out.eng.alert_cooldown == 60 && out.eng.watch_cooldown == 30);
  CHECK(out.upload_interval_min == 10);
  CHECK(out.eng.crit_cooldown == cur.eng.crit_cooldown);   /* gói không mang, giữ nguyên */

  uint8_t again[16];
  ble_proto_encode_config(&out, again);
  CHECK(memcmp(again, bytes, 16) == 0);
}

static void test_config_rejects_invalid(void) {
  wb_config_t cur, out;
  wb_config_set_default(&cur);
  uint8_t b[16] = {0x0A, 0x00, 0x58, 0x55, 0x03, 0x32, 0x2D, 0x78, 0x01, 0x47, 0x56, 0x01, 0x01, 0x3C, 0x0A, 0x1E};

  b[2] = 96;   /* T ngoài 85..95 */
  CHECK(ble_proto_decode_config(b, 16, &cur, &out) == BLE_CFG_ERR_T_OUT_OF_RANGE);
  b[2] = 88;
  b[3] = 88;   /* crit_T phải < T */
  CHECK(ble_proto_decode_config(b, 16, &cur, &out) == BLE_CFG_ERR_FIELD);
  b[3] = 85;
  b[11] = 2; b[12] = 1;   /* k > n */
  CHECK(ble_proto_decode_config(b, 16, &cur, &out) == BLE_CFG_ERR_FIELD);
  b[11] = 1;
  CHECK(ble_proto_decode_config(b, 15, &cur, &out) == BLE_CFG_ERR_SIZE);
  CHECK(ble_proto_decode_config(b, 16, &cur, &out) == BLE_CFG_OK);
}

static void test_config_without_model_thresholds(void) {
  /* App gửi 127/127 và ML tắt khi server không có ngưỡng model: vẫn áp dụng phần luật. */
  wb_config_t cur, out;
  wb_config_set_default(&cur);
  const uint8_t b[16] = {0x02, 0x00, 0x5A, 0x55, 0x03, 0x32, 0x28, 0x8C, 0x00, 0x7F, 0x7F, 0x01, 0x01, 0x3C, 0x0A, 0x1E};
  CHECK(ble_proto_decode_config(b, 16, &cur, &out) == BLE_CFG_OK);
  CHECK(out.eng.T == 90 && !out.ml_enabled && !out.model_ok);
  CHECK(out.eng.watch_thr_q == 87 && out.eng.alert_thr_q == 101);   /* lấy từ bảng firmware */
}

static void test_record_bytes(void) {
  ble_record_t r = {.seq = 10231, .ts = 1792137600u, .spo2_mean = 95.4f, .spo2_min = 94.0f,
                    .spo2_valid_pct = 97, .pulse_mean = 78.0f, .sqi_pct = 91, .worn = true};
  uint8_t b[16];
  ble_proto_encode_record(&r, b);
  const uint8_t want[16] = {0xF7, 0x27, 0x00, 0x00, 0x80, 0xD9, 0xD1, 0x6A,
                            0xBA, 0x03, 0xAC, 0x03, 97, 78, 91, 0x01};
  CHECK(memcmp(b, want, 16) == 0);

  ble_record_t missing = {.seq = 1, .ts = 5, .spo2_mean = NAN, .spo2_min = NAN, .pulse_mean = NAN};
  ble_proto_encode_record(&missing, b);
  CHECK(b[8] == 0xFF && b[9] == 0xFF && b[10] == 0xFF && b[11] == 0xFF);
  CHECK(b[13] == 0xFF && b[15] == 0x00);
}

static void test_live_status_assessment(void) {
  uint8_t live[8];
  ble_live_t v = {.ts = 0x01020304, .spo2 = 97.25f, .pulse = 72.4f, .sqi_pct = 130};
  ble_proto_encode_live(&v, live);
  CHECK(live[0] == 0x04 && live[3] == 0x01);
  CHECK(live[4] == (973 & 0xFF) && live[5] == (973 >> 8));
  CHECK(live[6] == 72 && live[7] == 100);   /* sqi cắt về 100 */

  uint8_t st[14];
  ble_status_t s = {.battery_pct = BLE_PROTO_BATTERY_UNKNOWN, .worn = true, .on_wifi = false,
                    .backlog = 3, .acked_seq = 7, .last_seq = 10, .config_version = 9};
  ble_proto_encode_status(&s, st);
  CHECK(st[0] == 0xFF && st[1] == 0x01 && st[2] == 3 && st[4] == 7 && st[8] == 10 && st[12] == 9);

  uint8_t a[24];
  ble_assessment_t as = {.seq = 42, .ts = 100, .state = BLE_STATE_ALERT, .trigger = BLE_TRIG_ML_RISK,
                         .out_q = -128, .T = 88, .spo2_now = 89.8f, .spo2_min_10m = NAN,
                         .spo2_slope_30m = -0.123f, .pulse_now = 96, .reasons = BLE_REASON_BELOW_ALERT,
                         .config_version = 9};
  ble_proto_encode_assessment(&as, a);
  CHECK(a[8] == 1 && a[9] == 3 && (int8_t)a[10] == -128 && a[11] == 88);
  CHECK(a[12] == (898 & 0xFF) && a[14] == 0xFF && a[15] == 0xFF);
  CHECK((int16_t)(a[16] | (a[17] << 8)) == -12);
  CHECK(a[20] == 0x04 && a[22] == 9);
}

static void test_control_and_mapping(void) {
  const uint8_t ack[5] = {0x01, 0x10, 0x27, 0x00, 0x00};
  uint8_t op;
  uint32_t arg;
  CHECK(ble_proto_decode_control(ack, 5, &op, &arg) && op == BLE_OP_ACK && arg == 10000);
  CHECK(!ble_proto_decode_control(ack, 4, &op, &arg));

  CHECK(ble_proto_state_from_level(SPO2TC_WATCH) == BLE_STATE_WATCH);
  CHECK(ble_proto_state_from_level(SPO2TC_UNKNOWN) == BLE_STATE_UNKNOWN);
  spo2tc_result_t r = {0};
  r.level = SPO2TC_CRITICAL; r.critical_reason = 2;
  CHECK(ble_proto_trigger_from_result(&r) == BLE_TRIG_HR_OUT_OF_RANGE);
  r.level = SPO2TC_ALERT; r.critical_reason = 0; r.alert_by_rule = 1;
  CHECK(ble_proto_trigger_from_result(&r) == BLE_TRIG_RULE_ALERT);
  r.alert_by_rule = 0;
  CHECK(ble_proto_trigger_from_result(&r) == BLE_TRIG_ML_RISK);
  r.level = SPO2TC_NORMAL;
  CHECK(ble_proto_trigger_from_result(&r) == BLE_TRIG_NONE);
}

static void test_wifi_prov(void) {
  /* 0x01 | 4 | "Home" | 8 | "12345678" */
  const uint8_t put[] = {0x01, 4, 'H', 'o', 'm', 'e', 8, '1', '2', '3', '4', '5', '6', '7', '8'};
  ble_wifi_cmd_t cmd;
  CHECK(ble_proto_decode_wifi(put, sizeof(put), &cmd));
  CHECK(cmd.op == BLE_WIFI_OP_PUT && strcmp(cmd.ssid, "Home") == 0 && strcmp(cmd.pass, "12345678") == 0);

  const uint8_t open_net[] = {0x01, 3, 'C', 'a', 'f', 0};
  CHECK(ble_proto_decode_wifi(open_net, sizeof(open_net), &cmd) && cmd.pass[0] == 0);

  const uint8_t short_pass[] = {0x01, 1, 'A', 3, 'a', 'b', 'c'};
  CHECK(!ble_proto_decode_wifi(short_pass, sizeof(short_pass), &cmd));
  CHECK(!ble_proto_decode_wifi(put, sizeof(put) - 1, &cmd));   /* thiếu byte */
  const uint8_t clear[] = {0x02};
  CHECK(ble_proto_decode_wifi(clear, 1, &cmd) && cmd.op == BLE_WIFI_OP_CLEAR);

  wifi_list_t l;
  wifi_list_clear(&l);
  CHECK(wifi_list_put(&l, "A", "aaaaaaaa"));
  CHECK(wifi_list_put(&l, "B", ""));
  CHECK(wifi_list_put(&l, "C", "cccccccc"));
  CHECK(wifi_list_put(&l, "A", "newpass12"));   /* thay mật khẩu, thành mới nhất */
  CHECK(l.count == 3 && strcmp(l.net[2].ssid, "A") == 0 && strcmp(l.net[2].pass, "newpass12") == 0);
  CHECK(wifi_list_put(&l, "D", "dddddddd"));    /* đầy: bỏ B (cũ nhất) */
  CHECK(l.count == 3 && strcmp(l.net[0].ssid, "C") == 0);
  CHECK(!wifi_list_put(&l, "E", "short"));
  CHECK(wifi_list_valid(&l));

  uint8_t out[BLE_PROTO_WIFI_LIST_MAX_SIZE];
  size_t n = ble_proto_encode_wifi_list(&l, out);
  CHECK(n == 1 + 3 * 2 && out[0] == 3 && out[1] == 1 && out[2] == 'C');
  CHECK(memchr(out, 'n', n) == NULL);   /* không lộ mật khẩu */
}

int main(void) {
  test_wifi_prov();
  test_config_example_from_spec();
  test_config_rejects_invalid();
  test_config_without_model_thresholds();
  test_record_bytes();
  test_live_status_assessment();
  test_control_and_mapping();
  return test_report("ble_proto");
}

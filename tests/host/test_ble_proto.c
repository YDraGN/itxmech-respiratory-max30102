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
  uint8_t live[BLE_PROTO_LIVE_SIZE];
  CHECK(BLE_PROTO_LIVE_SIZE == 12);
  ble_live_t v = {.ts = 0x01020304, .spo2 = 97.25f, .spo2_min = 95.0f, .pulse = 72.4f, .sqi_pct = 130,
                  .valid_pct = 93, .worn = true};
  ble_proto_encode_live(&v, live);
  CHECK(live[0] == 0x04 && live[3] == 0x01);
  CHECK(live[4] == (973 & 0xFF) && live[5] == (973 >> 8));
  CHECK(live[6] == (950 & 0xFF) && live[7] == (950 >> 8));
  CHECK(live[8] == 72 && live[9] == 100);   /* sqi cắt về 100 */
  CHECK(live[10] == 93 && live[11] == 0x01);
  v.spo2 = v.spo2_min = NAN;
  v.pulse = NAN;
  v.worn = false;
  ble_proto_encode_live(&v, live);   /* cửa sổ không có giây hợp lệ */
  CHECK(live[4] == 0xFF && live[5] == 0xFF && live[6] == 0xFF && live[7] == 0xFF && live[8] == 0xFF);
  CHECK(live[11] == 0x00);

  uint8_t st[14];
  ble_status_t s = {.battery_pct = BLE_PROTO_BATTERY_UNKNOWN, .worn = true, .on_wifi = false,
                    .backlog = 3, .acked_seq = 7, .last_seq = 10, .config_version = 9};
  ble_proto_encode_status(&s, st);
  CHECK(st[0] == 0xFF && st[1] == 0x01 && st[2] == 3 && st[4] == 7 && st[8] == 10 && st[12] == 9);
  s.on_wifi = true;
  s.wifi_ready = true;   /* bit 3: có WiFi + khóa, sẵn sàng dự phòng khi mất BLE */
  ble_proto_encode_status(&s, st);
  CHECK(st[1] == (0x01 | 0x04 | 0x08));

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

static void test_device_key(void) {
  /* Cùng byte với BlePacketCodec.encodeDeviceCredentials của app: 03 | 3 | "k3y" | 23 | url */
  const char *url = "https://h.vn/api/app/v1/";
  uint8_t b[64];
  size_t n = 0;
  b[n++] = 0x03;
  b[n++] = 3;
  memcpy(b + n, "k3y", 3);
  n += 3;
  b[n++] = (uint8_t)strlen(url);
  memcpy(b + n, url, strlen(url));
  n += strlen(url);
  ble_wifi_cmd_t cmd;
  CHECK(ble_proto_decode_wifi(b, n, &cmd));
  CHECK(cmd.op == BLE_WIFI_OP_SET_KEY);
  CHECK(strcmp(cmd.key, "k3y") == 0);
  CHECK(strcmp(cmd.url, "https://h.vn/api/app/v1") == 0);   /* bỏ "/" cuối */
  CHECK(!ble_proto_decode_wifi(b, n - 1, &cmd));             /* thiếu byte */

  b[2] = '"';                                                /* ký tự phá JSON/header */
  CHECK(!ble_proto_decode_wifi(b, n, &cmd));
  b[2] = 'k';
  memcpy(b + 7, "ftp://x.", 8);                              /* không phải http(s) */
  CHECK(!ble_proto_decode_wifi(b, n, &cmd));
}

static void test_record_assessment_roundtrip(void) {
  ble_record_t r = {.seq = 77, .ts = 1792137600u, .spo2_mean = 95.4f, .spo2_min = NAN, .spo2_valid_pct = 98,
                    .pulse_mean = 72.0f, .sqi_pct = 91, .worn = true};
  uint8_t rb[BLE_PROTO_RECORD_SIZE];
  ble_proto_encode_record(&r, rb);
  ble_record_t r2;
  CHECK(ble_proto_decode_record(rb, sizeof(rb), &r2));
  CHECK(r2.seq == 77 && r2.ts == 1792137600u && fabsf(r2.spo2_mean - 95.4f) < 1e-4f && isnan(r2.spo2_min));
  CHECK(r2.spo2_valid_pct == 98 && r2.pulse_mean == 72.0f && r2.sqi_pct == 91 && r2.worn);
  CHECK(!ble_proto_decode_record(rb, sizeof(rb) - 1, &r2));

  ble_assessment_t a = {.seq = 78, .ts = 1792137660u, .state = BLE_STATE_ALERT, .trigger = BLE_TRIG_RULE_ALERT,
                        .out_q = -12, .T = 88, .spo2_now = 87.5f, .spo2_min_10m = 86.0f, .spo2_slope_30m = -0.25f,
                        .pulse_now = NAN, .local_notified = true,
                        .reasons = BLE_REASON_BELOW_ALERT | BLE_REASON_SPO2_DOWNTREND, .config_version = 9};
  uint8_t ab[BLE_PROTO_ASSESSMENT_SIZE];
  ble_proto_encode_assessment(&a, ab);
  ble_assessment_t a2;
  CHECK(ble_proto_decode_assessment(ab, sizeof(ab), &a2));
  CHECK(a2.seq == 78 && a2.state == BLE_STATE_ALERT && a2.trigger == BLE_TRIG_RULE_ALERT && a2.out_q == -12);
  CHECK(a2.T == 88 && fabsf(a2.spo2_now - 87.5f) < 1e-4f && fabsf(a2.spo2_slope_30m + 0.25f) < 1e-4f);
  CHECK(isnan(a2.pulse_now) && a2.local_notified && a2.reasons == a.reasons && a2.config_version == 9);
}

/* DIAG 24 B: byte cố định để app và server đọc đúng (ble-gatt.md mục 3.9). */
static void test_diag(void) {
  ble_diag_t d = {.ts = 1792137900u, .uptime_s = 4500, .boot_id = 0x0102, .cpu_avg_pct = 2.34f,
                  .cpu_peak_pct = 11.0f, .model_ms = 17.9f, .minute_ms = 61.2f, .flash_ms = 42.0f,
                  .heap_min_kb = 61, .wifi_s = 9, .reset_reason = 1, .ble_drops = 0};
  uint8_t b[BLE_PROTO_DIAG_SIZE];
  ble_proto_encode_diag(&d, b);
  CHECK(ble_proto_get_u32(b) == 1792137900u && ble_proto_get_u32(b + 4) == 4500);
  CHECK(b[8] == 0x02 && b[9] == 0x01);
  CHECK(b[10] == 23 && b[11] == 0);            /* 2,34 % → 23 (×10) */
  CHECK(b[12] == 110 && b[14] == 179 && b[16] == (612 & 0xFF) && b[17] == (612 >> 8));
  CHECK(b[18] == 164 && b[19] == 1);           /* 420 */
  CHECK(b[20] == 61 && b[21] == 9 && b[22] == 1 && b[23] == 0);

  ble_diag_t d2;
  CHECK(ble_proto_decode_diag(b, sizeof(b), &d2));
  CHECK(d2.uptime_s == 4500 && d2.boot_id == 0x0102 && fabsf(d2.cpu_avg_pct - 2.3f) < 1e-4f);
  CHECK(fabsf(d2.model_ms - 17.9f) < 1e-4f && d2.heap_min_kb == 61 && d2.wifi_s == 9);
  CHECK(!ble_proto_decode_diag(b, sizeof(b) - 1, &d2));

  d.model_ms = NAN;   /* cửa sổ không chạy model */
  ble_proto_encode_diag(&d, b);
  CHECK(b[14] == 0xFF && b[15] == 0xFF);
  CHECK(ble_proto_decode_diag(b, sizeof(b), &d2) && isnan(d2.model_ms));
}

int main(void) {
  test_diag();
  test_wifi_prov();
  test_device_key();
  test_record_assessment_roundtrip();
  test_config_example_from_spec();
  test_config_rejects_invalid();
  test_config_without_model_thresholds();
  test_record_bytes();
  test_live_status_assessment();
  test_control_and_mapping();
  return test_report("ble_proto");
}

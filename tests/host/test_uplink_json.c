/* JSON gửi qua WiFi phải giống app gửi (OutboxUploader.kt), và đọc được phản hồi của server. */
#include <math.h>
#include <string.h>

#include "test_util.h"
#include "uplink_json.h"

static const uplink_meta_t META = {
    .device_id = "WB-C3-1A2B", .fw = "1.2.0", .model_version = "hybrid_tc-x", .boot_id = 7, .config_version = 9};

static void test_telemetry(void) {
  ble_record_t r[2] = {
      {.seq = 10, .ts = 1792137600u, .spo2_mean = 95.4f, .spo2_min = 94.0f, .spo2_valid_pct = 98,
       .pulse_mean = 72.0f, .sqi_pct = 91, .worn = true},
      {.seq = 11, .ts = 1792137660u, .spo2_mean = NAN, .spo2_min = NAN, .spo2_valid_pct = 0, .pulse_mean = NAN,
       .sqi_pct = 0, .worn = false},
  };
  char buf[1024];
  size_t n = uplink_json_telemetry(buf, sizeof(buf), &META, r, 2);
  CHECK(n == strlen(buf));
  CHECK(strcmp(buf,
               "{\"device_id\":\"WB-C3-1A2B\",\"boot_id\":7,\"fw\":\"1.2.0\",\"model_version\":\"hybrid_tc-x\","
               "\"config_version\":9,\"transport\":\"wifi\",\"samples\":["
               "{\"seq\":10,\"ts\":1792137600,\"spo2_mean\":95.4,\"spo2_min\":94.0,\"spo2_valid\":0.98,"
               "\"pulse_mean\":72.0,\"sqi\":0.91,\"worn\":true},"
               "{\"seq\":11,\"ts\":1792137660,\"spo2_mean\":null,\"spo2_min\":null,\"spo2_valid\":0.00,"
               "\"pulse_mean\":null,\"sqi\":0.00,\"worn\":false}]}") == 0);
  CHECK(uplink_json_telemetry(buf, 40, &META, r, 2) == 0);   /* thiếu chỗ */
  CHECK(uplink_json_telemetry(buf, sizeof(buf), &META, NULL, 0) > 0);
  CHECK(strstr(buf, "\"samples\":[]}") != NULL);
}

static void test_assessments(void) {
  ble_assessment_t a = {.seq = 12, .ts = 1792137720u, .state = BLE_STATE_CRITICAL, .trigger = BLE_TRIG_RULE_CRITICAL,
                        .out_q = -128, .T = 88, .spo2_now = 84.0f, .pulse_now = 110.0f, .local_notified = false,
                        .reasons = BLE_REASON_BELOW_CRITICAL | BLE_REASON_HR_HIGH, .config_version = 9};
  char buf[1024];
  CHECK(uplink_json_assessments(buf, sizeof(buf), &META, &a, 1) > 0);
  CHECK(strcmp(buf,
               "{\"device_id\":\"WB-C3-1A2B\",\"items\":[{\"seq\":12,\"ts\":1792137720,\"level\":\"CRITICAL\","
               "\"trigger\":\"rule_critical\",\"notify\":{\"watch\":false,\"alert\":false,\"critical\":false},"
               "\"ran_model\":false,\"out_q\":null,\"T\":88,\"spo2_now\":84.0,\"pulse_now\":110.0,"
               "\"reasons\":[\"BELOW_CRITICAL\",\"HR_HIGH\"],\"local_notified\":false,\"config_version\":9,"
               "\"model_version\":\"hybrid_tc-x\"}]}") == 0);
  a.state = BLE_STATE_WATCH;
  a.out_q = 5;
  a.local_notified = true;
  CHECK(uplink_json_assessments(buf, sizeof(buf), &META, &a, 1) > 0);
  CHECK(strstr(buf, "\"level\":\"WATCH\"") && strstr(buf, "\"watch\":true") && strstr(buf, "\"out_q\":5"));
}

static void test_batches_fit_buffer(void) {
  /* uplink.c dùng bộ đệm 10 KB: lô lớn nhất với giá trị dài nhất vẫn phải vừa. */
  static ble_record_t r[UPLINK_TELEMETRY_BATCH];
  static ble_assessment_t a[UPLINK_ASSESSMENT_BATCH];
  for (int i = 0; i < UPLINK_TELEMETRY_BATCH; i++)
    r[i] = (ble_record_t){.seq = 4000000000u, .ts = 4000000000u, .spo2_mean = 100, .spo2_min = 100,
                          .spo2_valid_pct = 100, .pulse_mean = 254, .sqi_pct = 100, .worn = true};
  for (int i = 0; i < UPLINK_ASSESSMENT_BATCH; i++)
    a[i] = (ble_assessment_t){.seq = 4000000000u, .ts = 4000000000u, .state = BLE_STATE_CRITICAL,
                              .trigger = BLE_TRIG_HR_OUT_OF_RANGE, .out_q = -127, .T = 95, .spo2_now = 100,
                              .pulse_now = 254, .local_notified = true, .reasons = 0x1FF, .config_version = 65535};
  static char buf[10240];
  CHECK(uplink_json_telemetry(buf, sizeof(buf), &META, r, UPLINK_TELEMETRY_BATCH) > 0);
  CHECK(uplink_json_assessments(buf, sizeof(buf), &META, a, UPLINK_ASSESSMENT_BATCH) > 0);
}

static void test_response_parsing(void) {
  const char *resp = "{\"accepted\":60,\"duplicates\":0,\"acked_seq\": 1234,\"config_version_latest\":10,"
                     "\"server_time\":1792137800}";
  long long v;
  CHECK(uplink_json_get_int(resp, "acked_seq", &v) && v == 1234);
  CHECK(uplink_json_get_int(resp, "server_time", &v) && v == 1792137800LL);
  CHECK(uplink_json_get_int(resp, "config_version_latest", &v) && v == 10);
  CHECK(!uplink_json_get_int(resp, "missing", &v));

  const char *cfg = "{\"schema\":\"config.v1\",\"model_version\":\"m\",\"version\":10,"
                    "\"ble_config_hex\":\"09005855033\"}";
  CHECK(uplink_json_get_int(cfg, "version", &v) && v == 10);   /* không nhầm với model_version */
  char s[40];
  CHECK(uplink_json_get_str(cfg, "ble_config_hex", s, sizeof(s)) && strcmp(s, "09005855033") == 0);
  CHECK(!uplink_json_get_str(cfg, "ble_config_hex", s, 5));    /* không đủ chỗ */

  uint8_t b[3];
  CHECK(uplink_hex_decode("0aFf10", b, 3) && b[0] == 0x0A && b[1] == 0xFF && b[2] == 0x10);
  CHECK(!uplink_hex_decode("0aFf1", b, 3) && !uplink_hex_decode("0aFg10", b, 3));
}

static void test_config_ack(void) {
  char buf[200];
  CHECK(uplink_json_config_ack(buf, sizeof(buf), "WB-1", 10, "applied", 1792137800u) > 0);
  CHECK(strcmp(buf, "{\"device_id\":\"WB-1\",\"config_version\":10,\"status\":\"applied\",\"applied_at\":1792137800}") == 0);
}

static void test_diagnostics(void) {
  ble_diag_t d = {.ts = 1792137900u, .uptime_s = 4500, .boot_id = 7, .cpu_avg_pct = 2.34f, .cpu_peak_pct = 11.0f,
                  .model_ms = NAN, .minute_ms = 3.2f, .flash_ms = 0.8f, .heap_min_kb = 61, .wifi_s = 9,
                  .reset_reason = 9, .ble_drops = 2};
  char buf[512];
  CHECK(uplink_json_diagnostics(buf, sizeof(buf), &META, &d, 1) > 0);
  CHECK(strcmp(buf,
               "{\"device_id\":\"WB-C3-1A2B\",\"fw\":\"1.2.0\",\"transport\":\"wifi\",\"items\":[{\"ts\":1792137900,"
               "\"uptime_s\":4500,\"boot_id\":7,\"cpu_avg_pct\":2.3,\"cpu_peak_pct\":11.0,\"model_ms\":null,"
               "\"minute_ms\":3.2,\"flash_ms\":0.8,\"heap_min_kb\":61,\"wifi_s\":9,\"reset_reason\":\"BROWNOUT\","
               "\"ble_drops\":2}]}") == 0);
  d.reset_reason = 200;
  CHECK(uplink_json_diagnostics(buf, sizeof(buf), &META, &d, 1) > 0 && strstr(buf, "\"reset_reason\":\"UNKNOWN\""));
  static ble_diag_t many[UPLINK_DIAG_BATCH];
  static char big[10240];
  CHECK(uplink_json_diagnostics(big, sizeof(big), &META, many, UPLINK_DIAG_BATCH) > 0);
}

int main(void) {
  test_diagnostics();
  test_telemetry();
  test_assessments();
  test_batches_fit_buffer();
  test_response_parsing();
  test_config_ack();
  return test_report("uplink_json");
}

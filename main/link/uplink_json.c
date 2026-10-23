#include "uplink_json.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Ghi nối tiếp vào buf; tràn thì đánh dấu và bỏ qua các lần ghi sau. */
typedef struct {
  char *buf;
  size_t cap, len;
  bool overflow;
} out_t;

static void put(out_t *o, const char *fmt, ...) {
  if (o->overflow) return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(o->buf + o->len, o->cap - o->len, fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= o->cap - o->len) {
    o->overflow = true;
    return;
  }
  o->len += (size_t)n;
}

static size_t finish(out_t *o) { return o->overflow ? 0 : o->len; }

/* Số 1 chữ số thập phân, NAN → null. */
static void put_num1(out_t *o, const char *key, float v) {
  if (isfinite(v)) put(o, "\"%s\":%.1f", key, (double)v);
  else put(o, "\"%s\":null", key);
}

static const char *level_name(uint8_t state) {
  switch (state) {
    case BLE_STATE_NORMAL: return "NORMAL";
    case BLE_STATE_ALERT: return "ALERT";
    case BLE_STATE_CRITICAL: return "CRITICAL";
    case BLE_STATE_WATCH: return "WATCH";
    default: return "UNKNOWN";
  }
}

static const char *trigger_name(uint8_t t) {
  switch (t) {
    case BLE_TRIG_RULE_CRITICAL: return "rule_critical";
    case BLE_TRIG_RULE_ALERT: return "rule_alert";
    case BLE_TRIG_ML_RISK: return "ml_risk";
    case BLE_TRIG_HR_OUT_OF_RANGE: return "hr_out_of_range";
    case BLE_TRIG_SIGNAL_LOST: return "signal_lost";
    default: return "none";
  }
}

/* Thứ tự bit giống enum AssessmentReason của app. */
static const char *const REASONS[] = {
    "SPO2_DOWNTREND", "NEAR_THRESHOLD", "BELOW_ALERT", "BELOW_CRITICAL", "HR_HIGH",
    "HR_LOW",         "LOW_SIGNAL",     "NOT_WORN",    "NO_DATA",
};

size_t uplink_json_telemetry(char *buf, size_t cap, const uplink_meta_t *m, const ble_record_t *r, size_t n) {
  out_t o = {buf, cap, 0, cap == 0};
  put(&o, "{\"device_id\":\"%s\",\"boot_id\":%u,\"fw\":\"%s\",\"model_version\":\"%s\",\"config_version\":%u,"
          "\"transport\":\"wifi\",\"samples\":[",
      m->device_id, (unsigned)m->boot_id, m->fw, m->model_version, (unsigned)m->config_version);
  for (size_t i = 0; i < n; i++) {
    put(&o, "%s{\"seq\":%lu,\"ts\":%lu,", i ? "," : "", (unsigned long)r[i].seq, (unsigned long)r[i].ts);
    put_num1(&o, "spo2_mean", r[i].spo2_mean);
    put(&o, ",");
    put_num1(&o, "spo2_min", r[i].spo2_min);
    put(&o, ",\"spo2_valid\":%.2f,", r[i].spo2_valid_pct / 100.0);
    put_num1(&o, "pulse_mean", r[i].pulse_mean);
    put(&o, ",\"sqi\":%.2f,\"worn\":%s}", r[i].sqi_pct / 100.0, r[i].worn ? "true" : "false");
  }
  put(&o, "]}");
  return finish(&o);
}

size_t uplink_json_assessments(char *buf, size_t cap, const uplink_meta_t *m, const ble_assessment_t *a, size_t n) {
  out_t o = {buf, cap, 0, cap == 0};
  put(&o, "{\"device_id\":\"%s\",\"items\":[", m->device_id);
  for (size_t i = 0; i < n; i++) {
    const ble_assessment_t *x = &a[i];
    const char *level = level_name(x->state);
    bool notified = x->local_notified;
    put(&o, "%s{\"seq\":%lu,\"ts\":%lu,\"level\":\"%s\",\"trigger\":\"%s\",", i ? "," : "", (unsigned long)x->seq,
        (unsigned long)x->ts, level, trigger_name(x->trigger));
    put(&o, "\"notify\":{\"watch\":%s,\"alert\":%s,\"critical\":%s},",
        notified && x->state == BLE_STATE_WATCH ? "true" : "false",
        notified && x->state == BLE_STATE_ALERT ? "true" : "false",
        notified && x->state == BLE_STATE_CRITICAL ? "true" : "false");
    if (x->out_q == -128) put(&o, "\"ran_model\":false,\"out_q\":null,");
    else put(&o, "\"ran_model\":true,\"out_q\":%d,", (int)x->out_q);
    put(&o, "\"T\":%u,", (unsigned)x->T);
    put_num1(&o, "spo2_now", x->spo2_now);
    put(&o, ",");
    put_num1(&o, "pulse_now", x->pulse_now);
    put(&o, ",\"reasons\":[");
    bool first = true;
    for (size_t b = 0; b < sizeof(REASONS) / sizeof(REASONS[0]); b++) {
      if (!(x->reasons & (1u << b))) continue;
      put(&o, "%s\"%s\"", first ? "" : ",", REASONS[b]);
      first = false;
    }
    put(&o, "],\"local_notified\":%s,\"config_version\":%u,\"model_version\":\"%s\"}", notified ? "true" : "false",
        (unsigned)x->config_version, m->model_version);
  }
  put(&o, "]}");
  return finish(&o);
}

size_t uplink_json_diagnostics(char *buf, size_t cap, const uplink_meta_t *m, const ble_diag_t *d, size_t n) {
  out_t o = {buf, cap, 0, cap == 0};
  put(&o, "{\"device_id\":\"%s\",\"fw\":\"%s\",\"transport\":\"wifi\",\"items\":[", m->device_id, m->fw);
  for (size_t i = 0; i < n; i++) {
    const ble_diag_t *x = &d[i];
    put(&o, "%s{\"ts\":%lu,\"uptime_s\":%lu,\"boot_id\":%u,", i ? "," : "", (unsigned long)x->ts,
        (unsigned long)x->uptime_s, (unsigned)x->boot_id);
    put_num1(&o, "cpu_avg_pct", x->cpu_avg_pct);
    put(&o, ",");
    put_num1(&o, "cpu_peak_pct", x->cpu_peak_pct);
    put(&o, ",");
    put_num1(&o, "model_ms", x->model_ms);
    put(&o, ",");
    put_num1(&o, "minute_ms", x->minute_ms);
    put(&o, ",");
    put_num1(&o, "flash_ms", x->flash_ms);
    put(&o, ",\"heap_min_kb\":%u,\"wifi_s\":%u,\"reset_reason\":\"%s\",\"ble_drops\":%u}", (unsigned)x->heap_min_kb,
        (unsigned)x->wifi_s, ble_proto_reset_reason_name(x->reset_reason), (unsigned)x->ble_drops);
  }
  put(&o, "]}");
  return finish(&o);
}

size_t uplink_json_config_ack(char *buf, size_t cap, const char *device_id, uint16_t version, const char *status,
                              uint32_t applied_at) {
  out_t o = {buf, cap, 0, cap == 0};
  put(&o, "{\"device_id\":\"%s\",\"config_version\":%u,\"status\":\"%s\",\"applied_at\":%lu}", device_id,
      (unsigned)version, status, (unsigned long)applied_at);
  return finish(&o);
}

/* Vị trí ngay sau dấu ":" của `"key"`, bỏ khoảng trắng; NULL nếu không có. */
static const char *value_of(const char *body, const char *key) {
  size_t klen = strlen(key);
  for (const char *p = strchr(body, '"'); p; p = strchr(p + 1, '"')) {
    if (strncmp(p + 1, key, klen) != 0 || p[1 + klen] != '"') continue;
    const char *v = p + 2 + klen;
    while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') v++;
    if (*v != ':') continue;
    v++;
    while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') v++;
    return v;
  }
  return NULL;
}

bool uplink_json_get_int(const char *body, const char *key, long long *out) {
  const char *v = value_of(body, key);
  if (!v || !(*v == '-' || (*v >= '0' && *v <= '9'))) return false;
  char *end = NULL;
  long long x = strtoll(v, &end, 10);
  if (end == v) return false;
  *out = x;
  return true;
}

bool uplink_json_get_str(const char *body, const char *key, char *out, size_t cap) {
  const char *v = value_of(body, key);
  if (!v || *v != '"' || cap == 0) return false;
  v++;
  const char *end = strchr(v, '"');
  if (!end || (size_t)(end - v) >= cap) return false;
  memcpy(out, v, (size_t)(end - v));
  out[end - v] = 0;
  return true;
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool uplink_hex_decode(const char *hex, uint8_t *out, size_t n) {
  if (strlen(hex) != 2 * n) return false;
  for (size_t i = 0; i < n; i++) {
    int hi = hex_digit(hex[2 * i]), lo = hex_digit(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)(hi << 4 | lo);
  }
  return true;
}

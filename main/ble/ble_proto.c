#include "ble_proto.h"

#include <math.h>
#include <string.h>

#define U16_MISSING 0xFFFFu
#define U8_MISSING  0xFFu

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static float from_x10(uint16_t v) { return v == U16_MISSING ? NAN : v / 10.0f; }

void ble_proto_put_u32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

uint32_t ble_proto_get_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* % ×10 dạng u16; NAN → 0xFFFF. */
static uint16_t x10(float v) {
  if (!isfinite(v) || v < 0.0f) return U16_MISSING;
  float r = roundf(v * 10.0f);
  return r >= (float)U16_MISSING ? (uint16_t)(U16_MISSING - 1) : (uint16_t)r;
}

/* Nhịp tim dạng u8; NAN hoặc <= 0 → 0xFF. */
static uint8_t bpm_u8(float v) {
  if (!isfinite(v) || v <= 0.0f) return U8_MISSING;
  float r = roundf(v);
  return r >= (float)U8_MISSING ? (uint8_t)(U8_MISSING - 1) : (uint8_t)r;
}

static uint8_t pct(uint8_t v) { return v > 100 ? 100 : v; }

void ble_proto_encode_live(const ble_live_t *v, uint8_t out[BLE_PROTO_LIVE_SIZE]) {
  ble_proto_put_u32(out, v->ts);
  put16(out + 4, x10(v->spo2));
  put16(out + 6, x10(v->spo2_min));
  out[8] = bpm_u8(v->pulse);
  out[9] = pct(v->sqi_pct);
  out[10] = pct(v->valid_pct);
  out[11] = v->worn ? 0x01 : 0x00;
}

void ble_proto_encode_diag(const ble_diag_t *d, uint8_t out[BLE_PROTO_DIAG_SIZE]) {
  ble_proto_put_u32(out, d->ts);
  ble_proto_put_u32(out + 4, d->uptime_s);
  put16(out + 8, d->boot_id);
  put16(out + 10, x10(d->cpu_avg_pct));
  put16(out + 12, x10(d->cpu_peak_pct));
  put16(out + 14, x10(d->model_ms));
  put16(out + 16, x10(d->minute_ms));
  put16(out + 18, x10(d->flash_ms));
  out[20] = d->heap_min_kb;
  out[21] = d->wifi_s;
  out[22] = d->reset_reason;
  out[23] = d->ble_drops;
}

/* Cùng thứ tự với esp_reset_reason_t (ESP-IDF 5.x) và ResetReason ở app. */
static const char *const RESET_NAMES[] = {
    "UNKNOWN", "POWERON", "EXT", "SW", "PANIC", "INT_WDT", "TASK_WDT", "WDT",
    "DEEPSLEEP", "BROWNOUT", "SDIO", "USB", "JTAG", "EFUSE", "PWR_GLITCH", "CPU_LOCKUP",
};

const char *ble_proto_reset_reason_name(uint8_t reason) {
  return reason < sizeof(RESET_NAMES) / sizeof(RESET_NAMES[0]) ? RESET_NAMES[reason] : "UNKNOWN";
}

bool ble_proto_decode_diag(const uint8_t *in, size_t len, ble_diag_t *out) {
  if (len != BLE_PROTO_DIAG_SIZE) return false;
  out->ts = ble_proto_get_u32(in);
  out->uptime_s = ble_proto_get_u32(in + 4);
  out->boot_id = get16(in + 8);
  out->cpu_avg_pct = from_x10(get16(in + 10));
  out->cpu_peak_pct = from_x10(get16(in + 12));
  out->model_ms = from_x10(get16(in + 14));
  out->minute_ms = from_x10(get16(in + 16));
  out->flash_ms = from_x10(get16(in + 18));
  out->heap_min_kb = in[20];
  out->wifi_s = in[21];
  out->reset_reason = in[22];
  out->ble_drops = in[23];
  return true;
}

void ble_proto_encode_record(const ble_record_t *r, uint8_t out[BLE_PROTO_RECORD_SIZE]) {
  ble_proto_put_u32(out, r->seq);
  ble_proto_put_u32(out + 4, r->ts);
  put16(out + 8, x10(r->spo2_mean));
  put16(out + 10, x10(r->spo2_min));
  out[12] = pct(r->spo2_valid_pct);
  out[13] = bpm_u8(r->pulse_mean);
  out[14] = pct(r->sqi_pct);
  out[15] = r->worn ? 0x01 : 0x00;
}

void ble_proto_encode_assessment(const ble_assessment_t *a, uint8_t out[BLE_PROTO_ASSESSMENT_SIZE]) {
  ble_proto_put_u32(out, a->seq);
  ble_proto_put_u32(out + 4, a->ts);
  out[8] = a->state;
  out[9] = a->trigger;
  out[10] = (uint8_t)a->out_q;
  out[11] = a->T;
  put16(out + 12, x10(a->spo2_now));
  put16(out + 14, x10(a->spo2_min_10m));
  int16_t slope = 0;
  if (isfinite(a->spo2_slope_30m)) {
    float s = roundf(a->spo2_slope_30m * 100.0f);
    slope = s > 32767.0f ? 32767 : (s < -32768.0f ? -32768 : (int16_t)s);
  }
  put16(out + 16, (uint16_t)slope);
  out[18] = bpm_u8(a->pulse_now);
  out[19] = a->local_notified ? 1 : 0;
  put16(out + 20, a->reasons);
  put16(out + 22, a->config_version);
}

void ble_proto_encode_status(const ble_status_t *s, uint8_t out[BLE_PROTO_STATUS_SIZE]) {
  out[0] = s->battery_pct;
  out[1] = (uint8_t)((s->worn ? 0x01 : 0) | (s->charging ? 0x02 : 0) | (s->on_wifi ? 0x04 : 0) |
                       (s->wifi_ready ? 0x08 : 0));
  put16(out + 2, s->backlog);
  ble_proto_put_u32(out + 4, s->acked_seq);
  ble_proto_put_u32(out + 8, s->last_seq);
  put16(out + 12, s->config_version);
}

bool ble_proto_decode_control(const uint8_t *in, size_t len, uint8_t *op, uint32_t *arg) {
  if (len != BLE_PROTO_CONTROL_SIZE) return false;
  *op = in[0];
  *arg = ble_proto_get_u32(in + 1);
  return true;
}

static bool in_range(int v, int lo, int hi) { return v >= lo && v <= hi; }

ble_cfg_err_t ble_proto_decode_config(const uint8_t *in, size_t len, const wb_config_t *current, wb_config_t *out) {
  if (len != BLE_PROTO_CONFIG_SIZE) return BLE_CFG_ERR_SIZE;

  wb_config_t c = *current;
  c.version = (uint16_t)(in[0] | (in[1] << 8));
  int T = in[2];
  if (spo2tc_config_set_T(&c.eng, T) != 0) return BLE_CFG_ERR_T_OUT_OF_RANGE;

  int crit_T = in[3], n_min = in[4], sqi = in[5], hr_low = in[6], hr_high = in[7];
  int k = in[11], n = in[12], alert_cd = in[13], upload = in[14], watch_cd = in[15];

  if (!in_range(crit_T, 70, T - 1)) return BLE_CFG_ERR_FIELD;
  if (!in_range(n_min, 1, 10)) return BLE_CFG_ERR_FIELD;
  if (sqi > 100) return BLE_CFG_ERR_FIELD;
  if (hr_low != 0 && !in_range(hr_low, 30, 80)) return BLE_CFG_ERR_FIELD;
  if (hr_high != 0 && (!in_range(hr_high, 100, 220) || hr_high <= hr_low)) return BLE_CFG_ERR_FIELD;
  if (!(1 <= k && k <= n && n <= SPO2TC_MAX_EVAL)) return BLE_CFG_ERR_FIELD;
  if (!in_range(alert_cd, 5, 240) || !in_range(watch_cd, 5, 240)) return BLE_CFG_ERR_FIELD;
  if (!in_range(upload, 1, 60)) return BLE_CFG_ERR_FIELD;

  c.eng.crit_T = crit_T;
  c.eng.crit_n_min = n_min;
  c.eng.crit_sqi = sqi / 100.0f;
  c.eng.hr_low = hr_low;
  c.eng.hr_high = hr_high;
  c.eng.watch_k = c.eng.alert_k = k;
  c.eng.watch_n = c.eng.alert_n = n;
  c.eng.alert_cooldown = alert_cd;
  c.eng.watch_cooldown = watch_cd;
  c.upload_interval_min = (uint8_t)upload;
  c.ml_enabled = (in[8] & 0x01) != 0;

  /* Ngưỡng INT8 chỉ để đối chiếu: firmware luôn dùng bảng của mình (đã điền bởi set_T). */
  int8_t watch_q = (int8_t)in[9], alert_q = (int8_t)in[10];
  c.model_ok = (watch_q == c.eng.watch_thr_q) && (alert_q == c.eng.alert_thr_q);

  *out = c;
  return BLE_CFG_OK;
}

void ble_proto_encode_config(const wb_config_t *c, uint8_t out[BLE_PROTO_CONFIG_SIZE]) {
  put16(out, c->version);
  out[2] = (uint8_t)c->eng.T;
  out[3] = (uint8_t)c->eng.crit_T;
  out[4] = (uint8_t)c->eng.crit_n_min;
  out[5] = (uint8_t)lroundf(c->eng.crit_sqi * 100.0f);
  out[6] = (uint8_t)c->eng.hr_low;
  out[7] = (uint8_t)c->eng.hr_high;
  out[8] = c->ml_enabled ? 0x01 : 0x00;
  out[9] = (uint8_t)c->eng.watch_thr_q;
  out[10] = (uint8_t)c->eng.alert_thr_q;
  out[11] = (uint8_t)c->eng.watch_k;
  out[12] = (uint8_t)c->eng.watch_n;
  out[13] = (uint8_t)c->eng.alert_cooldown;
  out[14] = c->upload_interval_min;
  out[15] = (uint8_t)c->eng.watch_cooldown;
}

static bool printable(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (p[i] <= 0x20 || p[i] >= 0x7F || p[i] == '"' || p[i] == '\\') return false;
  }
  return true;
}

/* 03 | key_len | key | url_len | url */
static bool decode_key(const uint8_t *in, size_t len, ble_wifi_cmd_t *out) {
  if (len < 3) return false;
  size_t key_len = in[1];
  if (key_len < 1 || key_len > DEVICE_KEY_MAX_LEN || len < 2 + key_len + 1) return false;
  size_t url_len = in[2 + key_len];
  if (len != 3 + key_len + url_len || url_len < 8 || url_len > SERVER_URL_MAX_LEN) return false;
  const uint8_t *key = in + 2, *url = in + 3 + key_len;
  if (!printable(key, key_len) || !printable(url, url_len)) return false;
  if (memcmp(url, "http://", 7) != 0 && memcmp(url, "https://", 8) != 0) return false;
  memcpy(out->key, key, key_len);
  memcpy(out->url, url, url_len);
  while (url_len > 0 && out->url[url_len - 1] == '/') out->url[--url_len] = 0;   /* bỏ "/" cuối */
  return true;
}

bool ble_proto_decode_wifi(const uint8_t *in, size_t len, ble_wifi_cmd_t *out) {
  memset(out, 0, sizeof(*out));
  if (len < 1) return false;
  out->op = in[0];
  if (out->op == BLE_WIFI_OP_CLEAR) return len == 1;
  if (out->op == BLE_WIFI_OP_SET_KEY) return decode_key(in, len, out);
  if (out->op != BLE_WIFI_OP_PUT || len < 3) return false;

  size_t ssid_len = in[1];
  if (ssid_len < 1 || ssid_len > WIFI_SSID_MAX_LEN || len < 2 + ssid_len + 1) return false;
  size_t pass_len = in[2 + ssid_len];
  if (len != 3 + ssid_len + pass_len) return false;
  if (pass_len != 0 && (pass_len < WIFI_PASS_MIN_LEN || pass_len > WIFI_PASS_MAX_LEN)) return false;
  if (memchr(in + 2, 0, ssid_len) || memchr(in + 3 + ssid_len, 0, pass_len)) return false;   /* không nhận byte 0 */

  memcpy(out->ssid, in + 2, ssid_len);
  memcpy(out->pass, in + 3 + ssid_len, pass_len);
  return true;
}

static float from_bpm(uint8_t v) { return v == U8_MISSING ? NAN : (float)v; }

bool ble_proto_decode_record(const uint8_t *in, size_t len, ble_record_t *out) {
  if (len != BLE_PROTO_RECORD_SIZE) return false;
  out->seq = ble_proto_get_u32(in);
  out->ts = ble_proto_get_u32(in + 4);
  out->spo2_mean = from_x10(get16(in + 8));
  out->spo2_min = from_x10(get16(in + 10));
  out->spo2_valid_pct = in[12];
  out->pulse_mean = from_bpm(in[13]);
  out->sqi_pct = in[14];
  out->worn = (in[15] & 0x01) != 0;
  return true;
}

bool ble_proto_decode_assessment(const uint8_t *in, size_t len, ble_assessment_t *out) {
  if (len != BLE_PROTO_ASSESSMENT_SIZE) return false;
  out->seq = ble_proto_get_u32(in);
  out->ts = ble_proto_get_u32(in + 4);
  out->state = in[8];
  out->trigger = in[9];
  out->out_q = (int8_t)in[10];
  out->T = in[11];
  out->spo2_now = from_x10(get16(in + 12));
  out->spo2_min_10m = from_x10(get16(in + 14));
  out->spo2_slope_30m = (int16_t)get16(in + 16) / 100.0f;
  out->pulse_now = from_bpm(in[18]);
  out->local_notified = in[19] != 0;
  out->reasons = get16(in + 20);
  out->config_version = get16(in + 22);
  return true;
}

size_t ble_proto_encode_wifi_list(const wifi_list_t *l, uint8_t out[BLE_PROTO_WIFI_LIST_MAX_SIZE]) {
  size_t n = 0;
  uint8_t count = l->count > WIFI_LIST_MAX ? WIFI_LIST_MAX : l->count;
  out[n++] = count;
  for (uint8_t i = 0; i < count; i++) {
    size_t s = strlen(l->net[i].ssid);
    out[n++] = (uint8_t)s;
    memcpy(out + n, l->net[i].ssid, s);
    n += s;
  }
  return n;
}

ble_vital_state_t ble_proto_state_from_level(spo2tc_level_t level) {
  switch (level) {
    case SPO2TC_NORMAL: return BLE_STATE_NORMAL;
    case SPO2TC_WATCH: return BLE_STATE_WATCH;
    case SPO2TC_ALERT: return BLE_STATE_ALERT;
    case SPO2TC_CRITICAL: return BLE_STATE_CRITICAL;
    case SPO2TC_UNKNOWN:
    default: return BLE_STATE_UNKNOWN;
  }
}

/* Theo bảng "trigger" ở CONFIG_VA_API.md §6.3. */
ble_trigger_t ble_proto_trigger_from_result(const spo2tc_result_t *r) {
  if (r->notify_critical || r->level == SPO2TC_CRITICAL)
    return r->critical_reason == 2 ? BLE_TRIG_HR_OUT_OF_RANGE : BLE_TRIG_RULE_CRITICAL;
  if (r->alert_by_rule && (r->notify_alert || r->level == SPO2TC_ALERT)) return BLE_TRIG_RULE_ALERT;
  if (r->notify_alert || r->notify_watch || r->level == SPO2TC_ALERT || r->level == SPO2TC_WATCH)
    return BLE_TRIG_ML_RISK;
  if (r->level == SPO2TC_UNKNOWN) return BLE_TRIG_SIGNAL_LOST;
  return BLE_TRIG_NONE;
}

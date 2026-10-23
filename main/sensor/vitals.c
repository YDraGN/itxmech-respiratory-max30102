#include "vitals.h"

#include <math.h>
#include "ble_proto.h"
#include "device_clock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define HISTORY_MIN 30
#define SLOPE_MIN_POINTS 10
#define NEAR_THRESHOLD_PCT 2.0f
#define DOWNTREND_PCT_PER_MIN (-0.1f)
#define LOW_SIGNAL_VALID 0.5f

typedef struct {
  double sp_sum, pu_sum;
  float sp_min;
  int n_sp, n_pu, n_sec, n_contact;
  uint32_t sqi_sum;
  uint32_t ts_start;
} minute_acc_t;

static minute_acc_t s_acc;
static minute_acc_t s_win;   /* cửa sổ LIVE, cùng kiểu gộp với phút */
static SemaphoreHandle_t s_mutex;
static volatile bool s_worn;

/* Lịch sử: [i] cũ → mới, chỉ dùng trong task phút nên không cần khóa. */
static float s_hist_mean[HISTORY_MIN], s_hist_min[HISTORY_MIN];
static int s_hist_head, s_hist_count;

static void acc_reset(minute_acc_t *a) {
  a->sp_sum = a->pu_sum = 0.0;
  a->sp_min = INFINITY;
  a->n_sp = a->n_pu = a->n_sec = a->n_contact = 0;
  a->sqi_sum = 0;
  a->ts_start = device_clock_now();
}

void vitals_init(void) {
  s_mutex = xSemaphoreCreateMutex();
  acc_reset(&s_acc);
  acc_reset(&s_win);
}

static void acc_add(minute_acc_t *a, bool valid, float spo2, float hr, bool contact, uint8_t sqi_pct) {
  a->n_sec++;
  a->sqi_sum += sqi_pct;
  if (contact) a->n_contact++;
  if (valid) {                               // giây kém → bỏ, KHÔNG ghi 0
    a->sp_sum += spo2;
    a->n_sp++;
    if (spo2 < a->sp_min) a->sp_min = spo2;
    if (isfinite(hr) && hr > 0) {
      a->pu_sum += hr;
      a->n_pu++;
    }
  }
}

void vitals_add_second(bool valid, float spo2, float hr, bool contact, uint8_t sqi_pct) {
  s_worn = contact;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  acc_add(&s_acc, valid, spo2, hr, contact, sqi_pct);
  acc_add(&s_win, valid, spo2, hr, contact, sqi_pct);
  xSemaphoreGive(s_mutex);
}

int vitals_finish_window(ble_live_t *out) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  const minute_acc_t *a = &s_win;
  int n_sec = a->n_sec;
  out->ts = device_clock_now();
  out->spo2 = a->n_sp ? (float)(a->sp_sum / a->n_sp) : NAN;
  out->spo2_min = a->n_sp ? a->sp_min : NAN;
  out->pulse = a->n_pu ? (float)(a->pu_sum / a->n_pu) : NAN;
  out->sqi_pct = n_sec ? (uint8_t)(a->sqi_sum / n_sec) : 0;
  out->valid_pct = n_sec ? (uint8_t)(100 * a->n_sp / n_sec) : 0;
  out->worn = n_sec > 0 && a->n_contact * 2 > n_sec;
  acc_reset(&s_win);
  xSemaphoreGive(s_mutex);
  return n_sec;
}

void vitals_finish_minute(vitals_minute_t *out) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  const minute_acc_t *a = &s_acc;
  out->m[SPO2TC_SPO2_MEAN]  = a->n_sp ? (float)(a->sp_sum / a->n_sp) : NAN;
  out->m[SPO2TC_SPO2_MIN]   = a->n_sp ? a->sp_min : NAN;
  out->m[SPO2TC_SPO2_VALID] = a->n_sp / 60.0f;          // không bao giờ NAN
  out->m[SPO2TC_PULSE_MEAN] = a->n_pu ? (float)(a->pu_sum / a->n_pu) : NAN;
  out->ts_start = a->ts_start;
  out->worn = a->n_sec > 0 && a->n_contact * 2 > a->n_sec;
  out->sqi_pct = a->n_sec ? (uint8_t)(a->sqi_sum / a->n_sec) : 0;
  acc_reset(&s_acc);
  xSemaphoreGive(s_mutex);
  if (out->m[SPO2TC_SPO2_VALID] > 1.0f) out->m[SPO2TC_SPO2_VALID] = 1.0f;
}

void vitals_history_push(const float m[SPO2TC_N_CH]) {
  s_hist_mean[s_hist_head] = m[SPO2TC_SPO2_MEAN];
  s_hist_min[s_hist_head] = m[SPO2TC_SPO2_MIN];
  s_hist_head = (s_hist_head + 1) % HISTORY_MIN;
  if (s_hist_count < HISTORY_MIN) s_hist_count++;
}

/* Phần tử thứ k tính từ phút mới nhất (k = 0). */
static int hist_index(int k) { return (s_hist_head - 1 - k + 2 * HISTORY_MIN) % HISTORY_MIN; }

float vitals_spo2_min_10m(void) {
  float v = NAN;
  for (int k = 0; k < s_hist_count && k < 10; k++) {
    float x = s_hist_min[hist_index(k)];
    if (isfinite(x) && (!isfinite(v) || x < v)) v = x;
  }
  return v;
}

/* Hồi quy tuyến tính spo2_mean theo phút trên tối đa 30 phút gần nhất. */
float vitals_spo2_slope_30m(void) {
  double n = 0, sx = 0, sy = 0, sxy = 0, sxx = 0;
  for (int k = 0; k < s_hist_count; k++) {
    float y = s_hist_mean[hist_index(k)];
    if (!isfinite(y)) continue;
    double t = -k;   /* phút, mới nhất = 0 */
    n++; sx += t; sy += y; sxy += t * y; sxx += t * t;
  }
  double den = n * sxx - sx * sx;
  if (n < SLOPE_MIN_POINTS || den == 0) return NAN;
  return (float)((n * sxy - sx * sy) / den);
}

uint16_t vitals_reasons(const vitals_minute_t *vm, const spo2tc_config_t *cfg, float slope_30m) {
  uint16_t r = 0;
  float spo2 = vm->m[SPO2TC_SPO2_MEAN], hr = vm->m[SPO2TC_PULSE_MEAN], valid = vm->m[SPO2TC_SPO2_VALID];
  if (isfinite(slope_30m) && slope_30m <= DOWNTREND_PCT_PER_MIN) r |= BLE_REASON_SPO2_DOWNTREND;
  if (isfinite(spo2)) {
    if (spo2 < cfg->crit_T) r |= BLE_REASON_BELOW_CRITICAL;
    if (spo2 < cfg->T) r |= BLE_REASON_BELOW_ALERT;
    else if (spo2 < cfg->T + NEAR_THRESHOLD_PCT) r |= BLE_REASON_NEAR_THRESHOLD;
  }
  if (isfinite(hr)) {
    if (cfg->hr_high > 0 && hr > cfg->hr_high) r |= BLE_REASON_HR_HIGH;
    if (cfg->hr_low > 0 && hr < cfg->hr_low) r |= BLE_REASON_HR_LOW;
  }
  if (valid <= 0.0f) r |= BLE_REASON_NO_DATA;
  else if (valid < LOW_SIGNAL_VALID) r |= BLE_REASON_LOW_SIGNAL;
  if (!vm->worn) r |= BLE_REASON_NOT_WORN;
  return r;
}

bool vitals_is_worn(void) { return s_worn; }

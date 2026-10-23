#include "diag.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "device_clock.h"
#include "device_state.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "diag";

#define MAX_TASKS 24
#define TOP_TASKS 4

#ifndef configRUN_TIME_COUNTER_TYPE   /* FreeRTOS cũ hơn 10.5 */
#define configRUN_TIME_COUNTER_TYPE uint32_t
#endif

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_reset_reason;

/* Cửa sổ đang đo (khóa s_mux). */
static struct {
  double cpu_sum;
  int cpu_n;
  float cpu_peak;
  int64_t model_us, minute_us, flash_us;   /* -1 = chưa có */
  uint32_t wifi_ms;
  uint32_t ble_drops;
} s_win;

/* Vòng đệm các cửa sổ đã đóng, chưa gửi (khóa s_mux). */
static ble_diag_t s_ring[DIAG_RING];
static size_t s_head, s_count;
static ble_diag_t s_last;
static bool s_has_last;

/* Run-time stats: chỉ diag_tick_second (một task) và diag_finish_window (task phút) đọc, mỗi hàm một bản riêng. */
static TaskStatus_t s_tasks[MAX_TASKS];
static uint32_t s_last_total, s_last_idle;
static bool s_have_last;

typedef struct {
  TaskHandle_t h;
  uint32_t run;
} task_mark_t;
static TaskStatus_t s_tasks_win[MAX_TASKS];
static task_mark_t s_marks[MAX_TASKS];
static size_t s_n_marks;
static uint32_t s_win_total;

static void win_reset(void) {
  memset(&s_win, 0, sizeof(s_win));
  s_win.model_us = s_win.minute_us = s_win.flash_us = -1;
}

void diag_init(void) {
  s_reset_reason = (uint8_t)esp_reset_reason();
  win_reset();
  ESP_LOGI(TAG, "lý do khởi động lần này: %s", ble_proto_reset_reason_name(s_reset_reason));
}

uint8_t diag_reset_reason(void) { return s_reset_reason; }

/* Tổng run-time của các task IDLE (một lõi: "IDLE" hoặc "IDLE0"). Trả số task, 0 nếu không đọc được. */
static UBaseType_t snapshot(TaskStatus_t *arr, uint32_t *total, uint32_t *idle) {
#if !(configUSE_TRACE_FACILITY && configGENERATE_RUN_TIME_STATS)
  /* sdkconfig cũ (tạo trước khi sdkconfig.defaults có 2 dòng FreeRTOS): build vẫn chạy, chỉ không có % CPU. */
  static bool warned;
  if (!warned) ESP_LOGW(TAG, "chưa bật run-time stats: xóa file sdkconfig rồi build lại để đo CPU");
  warned = true;
  (void)arr;
  *total = *idle = 0;
  return 0;
#else
  configRUN_TIME_COUNTER_TYPE t = 0;
  UBaseType_t n = uxTaskGetSystemState(arr, MAX_TASKS, &t);
  uint32_t i_sum = 0;
  for (UBaseType_t i = 0; i < n; i++)
    if (strncmp(arr[i].pcTaskName, "IDLE", 4) == 0) i_sum += (uint32_t)arr[i].ulRunTimeCounter;
  *total = (uint32_t)t;   /* bộ đếm µs 32 bit tràn sau ~71 phút: lấy hiệu số không dấu vẫn đúng */
  *idle = i_sum;
  return n;
#endif
}

void diag_tick_second(void) {
  uint32_t total, idle;
  if (snapshot(s_tasks, &total, &idle) == 0) return;   /* thiếu CONFIG_FREERTOS_USE_TRACE_FACILITY */
  if (s_have_last) {
    uint32_t dt = total - s_last_total, di = idle - s_last_idle;
    if (dt > 0) {
      float busy = 100.0f * (1.0f - (float)di / (float)dt);
      if (busy < 0) busy = 0;
      if (busy > 100) busy = 100;
      taskENTER_CRITICAL(&s_mux);
      s_win.cpu_sum += busy;
      s_win.cpu_n++;
      if (busy > s_win.cpu_peak) s_win.cpu_peak = busy;
      taskEXIT_CRITICAL(&s_mux);
    }
  }
  s_last_total = total;
  s_last_idle = idle;
  s_have_last = true;
}

static void note_max(int64_t *slot, int64_t us) {
  taskENTER_CRITICAL(&s_mux);
  if (us > *slot) *slot = us;
  taskEXIT_CRITICAL(&s_mux);
}

void diag_note_model_us(int64_t us) { note_max(&s_win.model_us, us); }
void diag_note_minute_us(int64_t us) { note_max(&s_win.minute_us, us); }
void diag_note_flash_us(int64_t us) { note_max(&s_win.flash_us, us); }

void diag_note_wifi_ms(uint32_t ms) {
  taskENTER_CRITICAL(&s_mux);
  s_win.wifi_ms += ms;
  taskEXIT_CRITICAL(&s_mux);
}

void diag_note_ble_drop(void) {
  taskENTER_CRITICAL(&s_mux);
  s_win.ble_drops++;
  taskEXIT_CRITICAL(&s_mux);
}

static float ms_or_nan(int64_t us) { return us < 0 ? NAN : (float)us / 1000.0f; }
static uint8_t sat8(uint32_t v) { return v > 255 ? 255 : (uint8_t)v; }

/* In các task tốn CPU nhất trong cửa sổ, để biết phần nào chiếm CPU khi đọc log (USB hoặc nhật ký BLE). */
static void log_top_tasks(void) {
  uint32_t total, idle;
  UBaseType_t n = snapshot(s_tasks_win, &total, &idle);
  if (n == 0) return;
  uint32_t dt = total - s_win_total;
  char line[160];
  size_t len = 0;
  bool used[MAX_TASKS] = {false};
  for (int k = 0; k < TOP_TASKS && dt > 0; k++) {
    int best = -1;
    uint32_t best_run = 0;
    for (UBaseType_t i = 0; i < n; i++) {
      if (used[i] || strncmp(s_tasks_win[i].pcTaskName, "IDLE", 4) == 0) continue;
      uint32_t prev = 0;
      for (size_t j = 0; j < s_n_marks; j++)
        if (s_marks[j].h == s_tasks_win[i].xHandle) prev = s_marks[j].run;
      uint32_t run = (uint32_t)s_tasks_win[i].ulRunTimeCounter - prev;
      if (best < 0 || run > best_run) best = (int)i, best_run = run;
    }
    if (best < 0) break;
    used[best] = true;
    int w = snprintf(line + len, sizeof(line) - len, "%s %s %.2f%%", k ? "," : "", s_tasks_win[best].pcTaskName,
                     100.0 * best_run / dt);
    if (w < 0 || (size_t)w >= sizeof(line) - len) break;
    len += (size_t)w;
  }
  if (len) ESP_LOGI(TAG, "task tốn CPU nhất:%s", line);
  s_n_marks = 0;
  for (UBaseType_t i = 0; i < n && i < MAX_TASKS; i++)
    s_marks[s_n_marks++] = (task_mark_t){s_tasks_win[i].xHandle, (uint32_t)s_tasks_win[i].ulRunTimeCounter};
  s_win_total = total;
}

void diag_finish_window(ble_diag_t *out) {
  ble_diag_t d;
  memset(&d, 0, sizeof(d));
  d.ts = device_clock_now();
  d.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
  d.boot_id = device_state_boot_id();
  d.heap_min_kb = sat8(esp_get_minimum_free_heap_size() / 1024);
  d.reset_reason = s_reset_reason;

  taskENTER_CRITICAL(&s_mux);
  d.cpu_avg_pct = s_win.cpu_n ? (float)(s_win.cpu_sum / s_win.cpu_n) : NAN;
  d.cpu_peak_pct = s_win.cpu_n ? s_win.cpu_peak : NAN;
  d.model_ms = ms_or_nan(s_win.model_us);
  d.minute_ms = ms_or_nan(s_win.minute_us);
  d.flash_ms = ms_or_nan(s_win.flash_us);
  d.wifi_s = sat8((s_win.wifi_ms + 500) / 1000);
  d.ble_drops = sat8(s_win.ble_drops);
  win_reset();
  /* Đầy thì bỏ mục cũ nhất: số liệu gần đây có ích hơn khi đọc lại. */
  s_ring[(s_head + s_count) % DIAG_RING] = d;
  if (s_count < DIAG_RING) s_count++;
  else s_head = (s_head + 1) % DIAG_RING;
  s_last = d;
  s_has_last = true;
  taskEXIT_CRITICAL(&s_mux);

  ESP_LOGI(TAG, "DIAG | CPU tb %.2f%% đỉnh %.1f%% | model %.1f ms | phút %.1f ms | flash %.1f ms | heap min %u KB | "
                "WiFi %u s | BLE bỏ %u | reset %s",
           (double)d.cpu_avg_pct, (double)d.cpu_peak_pct, (double)d.model_ms, (double)d.minute_ms, (double)d.flash_ms,
           (unsigned)d.heap_min_kb, (unsigned)d.wifi_s, (unsigned)d.ble_drops,
           ble_proto_reset_reason_name(d.reset_reason));
  log_top_tasks();
  if (out) *out = d;
}

size_t diag_pending(ble_diag_t *out, size_t max) {
  taskENTER_CRITICAL(&s_mux);
  size_t n = s_count < max ? s_count : max;
  for (size_t i = 0; i < n; i++) out[i] = s_ring[(s_head + i) % DIAG_RING];
  taskEXIT_CRITICAL(&s_mux);
  /* Mục ghi trước khi có giờ (cùng lần boot, vì vòng đệm nằm trong RAM) đổi sang epoch lúc gửi. */
  for (size_t i = 0; i < n; i++) out[i].ts = device_clock_fix_ts(out[i].ts);
  return n;
}

bool diag_latest(ble_diag_t *out) {
  taskENTER_CRITICAL(&s_mux);
  bool ok = s_has_last;
  if (ok) *out = s_last;
  taskEXIT_CRITICAL(&s_mux);
  if (ok) out->ts = device_clock_fix_ts(out->ts);
  return ok;
}

void diag_consume(size_t n) {
  taskENTER_CRITICAL(&s_mux);
  if (n > s_count) n = s_count;
  s_head = (s_head + n) % DIAG_RING;
  s_count -= n;
  taskEXIT_CRITICAL(&s_mux);
}

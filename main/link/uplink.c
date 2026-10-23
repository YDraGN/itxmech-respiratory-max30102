#include "uplink.h"

#include <stdlib.h>
#include <string.h>

#include "device_clock.h"
#include "device_cred.h"
#include "device_state.h"
#include "diag.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "led_status.h"
#include "record_store.h"
#include "spo2_tc_params.h"
#include "uplink_json.h"
#include "wifi_store.h"
#include "wristband_config.h"

static const char *TAG = "uplink";

#define NOTIFY_KICK    (1u << 0)
#define NOTIFY_URGENT  (1u << 1)

#define WIFI_GOT_IP    (1u << 0)
#define WIFI_FAILED    (1u << 1)

#define WIFI_CONNECT_TIMEOUT_MS 15000
#define HTTP_TIMEOUT_MS         15000
#define BODY_CAP                10240   /* đủ cho UPLINK_TELEMETRY_BATCH / UPLINK_ASSESSMENT_BATCH */
#define RESP_CAP                2048
#define MAX_ROUNDS              20      /* tối đa 20 lô mỗi lần bật WiFi (~20 giờ dữ liệu) */

static TaskHandle_t s_task;
static volatile bool s_active;
static volatile bool s_on_wifi;
static volatile bool s_ready;
static EventGroupHandle_t s_wifi_ev;
static bool s_wifi_inited;

/* Chỉ task uplink dùng: để static cho đỡ tốn stack. */
static ble_record_t s_recs[UPLINK_TELEMETRY_BATCH];
static ble_assessment_t s_asm[UPLINK_ASSESSMENT_BATCH];
static ble_diag_t s_diag[UPLINK_DIAG_BATCH];

/* ================================================================ WiFi */

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  (void)data;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) xEventGroupSetBits(s_wifi_ev, WIFI_FAILED);
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) xEventGroupSetBits(s_wifi_ev, WIFI_GOT_IP);
}

/* Khởi tạo WiFi lần đầu cần dùng (không tốn RAM khi vòng chỉ chạy BLE). */
static bool wifi_init_once(void) {
  if (s_wifi_inited) return true;
  esp_err_t err = esp_netif_init();
  if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "không khởi tạo được netif/event loop (%s)", esp_err_to_name(err));
    return false;
  }
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_init lỗi %s", esp_err_to_name(err));
    return false;
  }
  esp_wifi_set_storage(WIFI_STORAGE_RAM);   /* danh sách mạng nằm ở wifi_store, không để driver ghi NVS */
  esp_wifi_set_mode(WIFI_MODE_STA);
  s_wifi_ev = xEventGroupCreate();
  esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event, NULL);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL);
  s_wifi_inited = true;
  return true;
}

/* Thử lần lượt các mạng đã lưu, mới nhất trước. */
static bool wifi_connect(const wifi_list_t *l) {
  if (!wifi_init_once() || esp_wifi_start() != ESP_OK) return false;
  for (int i = (int)l->count - 1; i >= 0; i--) {
    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    memcpy(wc.sta.ssid, l->net[i].ssid, strnlen(l->net[i].ssid, sizeof(wc.sta.ssid)));
    memcpy(wc.sta.password, l->net[i].pass, strnlen(l->net[i].pass, sizeof(wc.sta.password)));
    wc.sta.threshold.authmode = l->net[i].pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    xEventGroupClearBits(s_wifi_ev, WIFI_GOT_IP | WIFI_FAILED);
    esp_wifi_connect();
    EventBits_t bits = xEventGroupWaitBits(s_wifi_ev, WIFI_GOT_IP | WIFI_FAILED, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if (bits & WIFI_GOT_IP) {
      ESP_LOGI(TAG, "đã vào WiFi \"%s\"", l->net[i].ssid);
      return true;
    }
    ESP_LOGW(TAG, "không vào được WiFi \"%s\"", l->net[i].ssid);
    esp_wifi_disconnect();
  }
  esp_wifi_stop();
  return false;
}

static void wifi_off(void) {
  esp_wifi_disconnect();
  esp_wifi_stop();
}

/* ================================================================ HTTP */

/* Trả mã HTTP (-1 nếu lỗi mạng). Phản hồi (cắt ở resp_cap - 1) đặt vào resp. */
static int http(const device_cred_t *cred, esp_http_client_method_t method, const char *path, const char *body,
                size_t body_len, const char *if_none_match, char *resp) {
  char url[SERVER_URL_MAX_LEN + 32];
  snprintf(url, sizeof(url), "%s/%s", cred->url, path);
  esp_http_client_config_t cfg = {
      .url = url,
      .method = method,
      .timeout_ms = HTTP_TIMEOUT_MS,
      .crt_bundle_attach = esp_crt_bundle_attach,   /* HTTPS: kiểm tra chứng chỉ bằng bộ CA gốc của ESP-IDF */
  };
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return -1;
  esp_http_client_set_header(c, "x-device-id", device_state_device_id());
  esp_http_client_set_header(c, "x-device-key", cred->key);
  if (body) esp_http_client_set_header(c, "content-type", "application/json");
  if (if_none_match) esp_http_client_set_header(c, "if-none-match", if_none_match);

  int status = -1;
  resp[0] = 0;
  if (esp_http_client_open(c, body ? (int)body_len : 0) == ESP_OK &&
      (!body || esp_http_client_write(c, body, (int)body_len) == (int)body_len)) {
    esp_http_client_fetch_headers(c);
    status = esp_http_client_get_status_code(c);
    int n = 0, r;
    while (n < RESP_CAP - 1 && (r = esp_http_client_read(c, resp + n, RESP_CAP - 1 - n)) > 0) n += r;
    resp[n] = 0;
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  if (status != 200 && status != 304) ESP_LOGW(TAG, "%s -> HTTP %d %.200s", path, status, resp);
  return status;
}

static void take_server_time(const char *resp) {
  long long t;
  if (uplink_json_get_int(resp, "server_time", &t) && t > 0) device_clock_set((uint32_t)t);
}

static void fill_meta(uplink_meta_t *m) {
  wb_config_t c;
  wb_config_get(&c);
  m->device_id = device_state_device_id();
  m->fw = esp_app_get_description()->version;
  m->model_version = SPO2TC_MODEL_VERSION;
  m->boot_id = device_state_boot_id();
  m->config_version = c.version;
}

/* Bản ghi lúc chưa có giờ (cùng lần boot) thì đổi ts sang epoch, như BACKFILL qua BLE. */
static uint32_t fixed_ts(uint32_t ts, uint16_t boot_id) {
  return boot_id == device_state_boot_id() ? device_clock_fix_ts(ts) : ts;
}

static bool post_assessments(const device_cred_t *cred, const uplink_meta_t *m, size_t n, char *body, char *resp) {
  size_t len = uplink_json_assessments(body, BODY_CAP, m, s_asm, n);
  return len > 0 && http(cred, HTTP_METHOD_POST, "ingest/assessments", body, len, NULL, resp) == 200;
}

/*
 * Một lượt: tối đa UPLINK_TELEMETRY_BATCH bản ghi phút từ acked_seq + 1, gửi assessment cùng khoảng seq trước,
 * rồi bản ghi phút; server trả acked_seq thì dời con trỏ (dữ liệu vẫn nằm trong flash cho tới khi bị ghi đè).
 * Trả số bản ghi đã gửi, 0 nếu hết, -1 nếu lỗi.
 */
static int upload_round(const device_cred_t *cred, const uplink_meta_t *m, char *body, char *resp,
                        long long *config_latest) {
  uint32_t from = record_store_acked_seq() + 1;
  rlog_cursor_t cur;
  rlog_entry_t e;
  size_t n = 0;
  record_store_cursor_init(&cur);
  while (n < UPLINK_TELEMETRY_BATCH && record_store_cursor_next(&cur, from, RLOG_MASK(RLOG_TELEMETRY), &e)) {
    if (!ble_proto_decode_record(e.payload, e.len, &s_recs[n])) continue;
    s_recs[n].ts = fixed_ts(s_recs[n].ts, e.boot_id);
    n++;
  }
  if (n == 0) return 0;
  uint32_t last = s_recs[n - 1].seq;

  size_t k = 0;
  record_store_cursor_init(&cur);
  while (record_store_cursor_next(&cur, from, RLOG_MASK(RLOG_ASSESSMENT), &e)) {
    if (e.seq > last) break;
    if (!ble_proto_decode_assessment(e.payload, e.len, &s_asm[k])) continue;
    s_asm[k].ts = fixed_ts(s_asm[k].ts, e.boot_id);
    if (++k == UPLINK_ASSESSMENT_BATCH) {
      if (!post_assessments(cred, m, k, body, resp)) return -1;
      k = 0;
    }
  }
  if (k > 0 && !post_assessments(cred, m, k, body, resp)) return -1;

  size_t len = uplink_json_telemetry(body, BODY_CAP, m, s_recs, n);
  if (len == 0 || http(cred, HTTP_METHOD_POST, "ingest/telemetry", body, len, NULL, resp) != 200) return -1;
  take_server_time(resp);
  long long acked;
  if (!uplink_json_get_int(resp, "acked_seq", &acked) || acked < 0) return -1;
  if (!record_store_ack((uint32_t)acked)) return -1;   /* server không nhận thêm: dừng, tránh gửi lặp */
  long long latest;
  if (uplink_json_get_int(resp, "config_version_latest", &latest) && latest > *config_latest) *config_latest = latest;
  ESP_LOGI(TAG, "gửi %u bản ghi (seq %lu..%lu), server acked %lld", (unsigned)n, (unsigned long)from,
           (unsigned long)last, acked);
  return (int)n;
}

/*
 * Ngưỡng mới bác sĩ đặt: GET device/config (etag = version đang dùng), server trả `ble_config_hex` là đúng
 * 16 byte CONFIG app vẫn ghi qua BLE, nên dùng lại ble_proto_decode_config + wb_config_accept.
 */
static void sync_config(const device_cred_t *cred, char *body, char *resp) {
  wb_config_t cur, next;
  wb_config_get(&cur);
  char etag[16];
  snprintf(etag, sizeof(etag), "\"%u\"", (unsigned)cur.version);
  if (http(cred, HTTP_METHOD_GET, "device/config", NULL, 0, etag, resp) != 200) return;

  char hex[2 * BLE_PROTO_CONFIG_SIZE + 1];
  uint8_t b[BLE_PROTO_CONFIG_SIZE];
  if (!uplink_json_get_str(resp, "ble_config_hex", hex, sizeof(hex)) || !uplink_hex_decode(hex, b, sizeof(b))) {
    ESP_LOGW(TAG, "server không trả ble_config_hex, giữ config v%u", (unsigned)cur.version);
    return;
  }
  uint16_t version = (uint16_t)(b[0] | (b[1] << 8));
  const char *status;
  ble_cfg_err_t err = ble_proto_decode_config(b, sizeof(b), &cur, &next);
  if (err != BLE_CFG_OK) {
    ESP_LOGW(TAG, "config v%u từ server không hợp lệ (mã %d)", (unsigned)version, err);
    status = "rejected";
  } else if (wb_config_accept(&next)) {
    ESP_LOGI(TAG, "nhận config v%u từ server", (unsigned)version);
    status = next.model_ok ? "applied" : "applied_rules_only";
  } else {
    return;   /* không mới hơn */
  }
  size_t len = uplink_json_config_ack(body, BODY_CAP, device_state_device_id(), version, status, device_clock_now());
  if (len) http(cred, HTTP_METHOD_POST, "ingest/config-ack", body, len, NULL, resp);
}

/* Các cửa sổ DIAG chưa gửi (lúc không có app). Lỗi thì giữ lại cho lượt sau, không chặn dữ liệu đo. */
static void post_diagnostics(const device_cred_t *cred, const uplink_meta_t *m, char *body, char *resp) {
  size_t n = diag_pending(s_diag, UPLINK_DIAG_BATCH);
  if (n == 0) return;
  size_t len = uplink_json_diagnostics(body, BODY_CAP, m, s_diag, n);
  if (len && http(cred, HTTP_METHOD_POST, "ingest/diagnostics", body, len, NULL, resp) == 200) diag_consume(n);
}

/* ================================================================ task */

static bool ready(wifi_list_t *l, device_cred_t *cred) {
  wifi_store_get(l);
  bool ok = device_cred_get(cred) && l->count > 0;
  s_ready = ok;
  led_status_set_wifi_ready(ok);
  return ok;
}

/* Bật WiFi, gửi phần còn chờ, lấy config mới, tắt WiFi. Trả true khi server đã nhận thêm dữ liệu. */
static bool uplink_flush(bool urgent) {
  wifi_list_t l;
  device_cred_t cred;
  if (!ready(&l, &cred)) {
    ESP_LOGI(TAG, "%u bản ghi chờ gửi - chưa cài WiFi hoặc chưa có khóa thiết bị, giữ trong flash",
             record_store_backlog());
    return false;
  }
  char *body = malloc(BODY_CAP), *resp = malloc(RESP_CAP);
  if (!body || !resp) {
    ESP_LOGE(TAG, "thiếu RAM cho bộ đệm gửi");
    free(body);
    free(resp);
    return false;
  }
  ESP_LOGI(TAG, "%s: bật WiFi, %u bản ghi chờ gửi", urgent ? "cảnh báo" : "định kỳ", record_store_backlog());
  int total = 0;
  int64_t t_wifi = esp_timer_get_time();
  if (wifi_connect(&l)) {
    s_on_wifi = true;
    uplink_meta_t m;
    fill_meta(&m);
    /* Chưa có giờ: gửi lô rỗng để lấy server_time trước, bản ghi lần boot này sẽ có ts thật. */
    if (!device_clock_is_synced()) {
      size_t len = uplink_json_telemetry(body, BODY_CAP, &m, NULL, 0);
      if (http(&cred, HTTP_METHOD_POST, "ingest/telemetry", body, len, NULL, resp) == 200) take_server_time(resp);
    }
    long long latest = -1;
    for (int round = 0; round < MAX_ROUNDS; round++) {
      int r = upload_round(&cred, &m, body, resp, &latest);
      if (r <= 0) break;
      total += r;
    }
    wb_config_t c;
    wb_config_get(&c);
    if (latest < 0 || latest > c.version) sync_config(&cred, body, resp);
    post_diagnostics(&cred, &m, body, resp);
    wifi_off();
    s_on_wifi = false;
  }
  /* Tính cả lần không vào được WiFi: radio vẫn bật suốt thời gian thử (tối đa 15 s mỗi mạng). */
  uint32_t wifi_ms = (uint32_t)((esp_timer_get_time() - t_wifi) / 1000);
  diag_note_wifi_ms(wifi_ms);
  free(body);
  free(resp);
  ESP_LOGI(TAG, "xong: đã gửi %d bản ghi, còn %u chờ (acked %lu / last %lu), WiFi bật %.1f s", total,
           record_store_backlog(), (unsigned long)record_store_acked_seq(), (unsigned long)record_store_last_seq(),
           wifi_ms / 1000.0);
  return total > 0;
}

static void uplink_task(void *arg) {
  (void)arg;
  for (;;) {
    wb_config_t cfg;
    wb_config_get(&cfg);
    TickType_t wait = s_active ? pdMS_TO_TICKS((uint32_t)cfg.upload_interval_min * 60u * 1000u) : portMAX_DELAY;
    uint32_t bits = 0;
    bool woke = xTaskNotifyWait(0, UINT32_MAX, &bits, wait) == pdTRUE;
    if (!s_active) continue;
    if (woke && !(bits & (NOTIFY_KICK | NOTIFY_URGENT))) continue;
    uplink_flush((bits & NOTIFY_URGENT) != 0);
  }
}

void uplink_init(void) {
  /* HTTPS (mbedTLS) cần stack lớn hơn task thường. */
  xTaskCreate(uplink_task, "uplink", 8192, NULL, 4, &s_task);
  uplink_refresh_ready();
}

void uplink_set_active(bool active) {
  bool was = s_active;
  s_active = active;
  if (active && !was) uplink_kick(false);   /* vào STANDALONE: thử gửi ngay một lần */
}

void uplink_kick(bool urgent) {
  if (s_task) xTaskNotify(s_task, urgent ? NOTIFY_URGENT : NOTIFY_KICK, eSetBits);
}

void uplink_refresh_ready(void) {
  wifi_list_t l;
  device_cred_t cred;
  ready(&l, &cred);
}

bool uplink_wifi_on(void) { return s_on_wifi; }

bool uplink_wifi_ready(void) { return s_ready; }

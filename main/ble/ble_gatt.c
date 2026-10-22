#include "ble_gatt.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "device_clock.h"
#include "device_state.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "link_manager.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "record_store.h"
#include "sdkconfig.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "uplink.h"
#include "vitals.h"
#include "wifi_store.h"
#include "wristband_config.h"

static const char *TAG = "ble";

#define ADV_FAST_MS       200
#define ADV_SLOW_MS       1000
#define ADV_UNITS(ms)     ((ms) * 1000 / 625)   /* đơn vị 0,625 ms */
#define TXQ_LEN           24
#define NOTIFY_RETRY_MS   20
#define NOTIFY_RETRIES    100                   /* ~2 s chờ buffer trống */
#define DEVICE_INFO_MAX   160

/* a51cXXXX-7b2e-4f6a-9c3d-5e8f1b2a6c40, byte đảo ngược (little-endian) theo NimBLE. */
#define RESPI_UUID(x) BLE_UUID128_INIT(0x40, 0x6c, 0x2a, 0x1b, 0x8f, 0x5e, 0x3d, 0x9c, 0x6a, 0x4f, 0x2e, 0x7b, \
                                       ((x) & 0xff), (((x) >> 8) & 0xff), 0x1c, 0xa5)

static ble_uuid128_t UUID_SVC = RESPI_UUID(0x0001);   /* không const: trường uuids128 của gói quảng bá */
static const ble_uuid128_t UUID_INFO = RESPI_UUID(0x0002);
static const ble_uuid128_t UUID_LIVE = RESPI_UUID(0x0003);
static const ble_uuid128_t UUID_RECORDS = RESPI_UUID(0x0004);
static const ble_uuid128_t UUID_ASSESSMENT = RESPI_UUID(0x0005);
static const ble_uuid128_t UUID_STATUS = RESPI_UUID(0x0006);
static const ble_uuid128_t UUID_CONTROL = RESPI_UUID(0x0007);
static const ble_uuid128_t UUID_CONFIG = RESPI_UUID(0x0008);
static const ble_uuid128_t UUID_WIFI = RESPI_UUID(0x0009);

typedef enum { CHR_INFO = 1, CHR_LIVE, CHR_RECORDS, CHR_ASSESSMENT, CHR_STATUS, CHR_CONTROL, CHR_CONFIG, CHR_WIFI } chr_id_t;

typedef enum { TX_LIVE, TX_RECORD, TX_ASSESSMENT, TX_STATUS, TX_BACKFILL } tx_kind_t;
typedef struct {
  uint8_t kind;
  uint8_t len;
  uint32_t arg;   /* from_seq cho BACKFILL */
  uint8_t data[BLE_PROTO_ASSESSMENT_SIZE];
} tx_msg_t;

static uint8_t s_own_addr_type;
static char s_name[20];
static volatile uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile uint16_t s_mtu = BLE_ATT_MTU_DFLT;
static uint16_t h_live, h_records, h_assessment, h_status;
static volatile bool s_sub_live, s_sub_records, s_sub_assessment, s_sub_status;
static QueueHandle_t s_txq;
static esp_timer_handle_t s_adv_timer;
static volatile bool s_adv_fast;

static int gap_event(struct ble_gap_event *ev, void *arg);

/* ================================================================ gói và thời gian */

/* ts nằm ở byte 4–7 của RECORD và ASSESSMENT. Ghi lúc chưa có giờ (cùng lần boot) thì đổi sang epoch. */
static void fix_ts(uint8_t *payload, uint16_t entry_boot_id) {
  if (entry_boot_id != device_state_boot_id()) return;
  uint32_t ts = ble_proto_get_u32(payload + BLE_PROTO_TS_OFFSET);
  ble_proto_put_u32(payload + BLE_PROTO_TS_OFFSET, device_clock_fix_ts(ts));
}

static void build_status(uint8_t out[BLE_PROTO_STATUS_SIZE]) {
  wb_config_t c;
  wb_config_get(&c);
  ble_status_t s = {
      .battery_pct = BLE_PROTO_BATTERY_UNKNOWN,   /* mạch chưa có chân đo pin */
      .worn = vitals_is_worn(),
      .charging = false,
      .on_wifi = uplink_wifi_on(),
      .backlog = record_store_backlog(),
      .acked_seq = record_store_acked_seq(),
      .last_seq = record_store_last_seq(),
      .config_version = c.version,
  };
  ble_proto_encode_status(&s, out);
}

static int records_per_packet(void) {
  int n = ((int)s_mtu - 3 - 1) / BLE_PROTO_RECORD_SIZE;
  if (n < 1) n = 1;
  if (n > BLE_PROTO_MAX_RECORDS) n = BLE_PROTO_MAX_RECORDS;
  return n;
}

/* ================================================================ gửi notify (chỉ task ble_tx) */

static bool notify(uint16_t conn, uint16_t handle, const uint8_t *data, size_t len) {
  for (int i = 0; i < NOTIFY_RETRIES; i++) {
    if (s_conn != conn) return false;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
    if (om) {
      int rc = ble_gatts_notify_custom(conn, handle, om);   /* om luôn bị stack giữ/giải phóng */
      if (rc == 0) return true;
      if (rc != BLE_HS_ENOMEM && rc != BLE_HS_EBUSY) {
        ESP_LOGW(TAG, "notify lỗi %d", rc);
        return false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(NOTIFY_RETRY_MS));
  }
  ESP_LOGW(TAG, "notify hết lượt chờ buffer");
  return false;
}

static void run_backfill(uint16_t conn, uint32_t requested) {
  uint32_t from = record_store_backfill_start(requested);
  int per_pkt = records_per_packet();
  uint8_t pkt[1 + BLE_PROTO_MAX_RECORDS * BLE_PROTO_RECORD_SIZE];
  int n = 0;
  unsigned n_rec = 0, n_asm = 0;
  bool ok = true;
  rlog_cursor_t c;
  rlog_entry_t e;

  ESP_LOGI(TAG, "BACKFILL từ seq %lu (app xin %lu), %d bản ghi/gói", (unsigned long)from,
           (unsigned long)requested, per_pkt);
  record_store_cursor_init(&c);
  while (ok && s_conn == conn &&
         record_store_cursor_next(&c, from, RLOG_MASK(RLOG_TELEMETRY) | RLOG_MASK(RLOG_ASSESSMENT), &e)) {
    fix_ts(e.payload, e.boot_id);
    if (e.type == RLOG_ASSESSMENT) {
      if (s_sub_assessment) ok = notify(conn, h_assessment, e.payload, BLE_PROTO_ASSESSMENT_SIZE);
      n_asm++;
      continue;
    }
    memcpy(pkt + 1 + n * BLE_PROTO_RECORD_SIZE, e.payload, BLE_PROTO_RECORD_SIZE);
    if (++n == per_pkt) {
      pkt[0] = (uint8_t)n;
      if (s_sub_records) ok = notify(conn, h_records, pkt, 1 + n * BLE_PROTO_RECORD_SIZE);
      n_rec += n;
      n = 0;
    }
  }
  if (ok && n > 0 && s_sub_records) {
    pkt[0] = (uint8_t)n;
    notify(conn, h_records, pkt, 1 + n * BLE_PROTO_RECORD_SIZE);
    n_rec += n;
  }
  ESP_LOGI(TAG, "BACKFILL xong: %u bản ghi, %u assessment%s", n_rec, n_asm, ok ? "" : " (dừng giữa chừng)");
}

static void tx_task(void *arg) {
  (void)arg;
  tx_msg_t m;
  for (;;) {
    if (xQueueReceive(s_txq, &m, portMAX_DELAY) != pdTRUE) continue;
    uint16_t conn = s_conn;
    if (conn == BLE_HS_CONN_HANDLE_NONE) continue;
    switch (m.kind) {
      case TX_LIVE:
        if (s_sub_live) notify(conn, h_live, m.data, m.len);
        break;
      case TX_RECORD:
        if (s_sub_records) {
          uint8_t pkt[1 + BLE_PROTO_RECORD_SIZE];
          pkt[0] = 1;
          memcpy(pkt + 1, m.data, BLE_PROTO_RECORD_SIZE);
          fix_ts(pkt + 1, device_state_boot_id());
          notify(conn, h_records, pkt, sizeof(pkt));
        }
        break;
      case TX_ASSESSMENT:
        if (s_sub_assessment) {
          fix_ts(m.data, device_state_boot_id());
          notify(conn, h_assessment, m.data, BLE_PROTO_ASSESSMENT_SIZE);
        }
        break;
      case TX_STATUS:
        if (s_sub_status) {
          uint8_t st[BLE_PROTO_STATUS_SIZE];
          build_status(st);
          notify(conn, h_status, st, sizeof(st));
        }
        break;
      case TX_BACKFILL:
        run_backfill(conn, m.arg);
        break;
    }
  }
}

/* Không chặn: gọi được từ callback của NimBLE. */
static void enqueue(tx_kind_t kind, const void *data, uint8_t len, uint32_t arg) {
  if (s_conn == BLE_HS_CONN_HANDLE_NONE) return;
  tx_msg_t m = {.kind = (uint8_t)kind, .len = len, .arg = arg};
  if (data && len) memcpy(m.data, data, len);
  if (xQueueSend(s_txq, &m, 0) != pdTRUE && kind != TX_LIVE)
    ESP_LOGW(TAG, "hàng đợi gửi đầy, bỏ gói loại %d (dữ liệu vẫn còn trong flash)", kind);
}

void ble_gatt_send_live(const ble_live_t *v) {
  uint8_t b[BLE_PROTO_LIVE_SIZE];
  ble_proto_encode_live(v, b);
  enqueue(TX_LIVE, b, sizeof(b), 0);
}

void ble_gatt_send_record(const ble_record_t *r) {
  uint8_t b[BLE_PROTO_RECORD_SIZE];
  ble_proto_encode_record(r, b);
  enqueue(TX_RECORD, b, sizeof(b), 0);
}

void ble_gatt_send_assessment(const ble_assessment_t *a) {
  uint8_t b[BLE_PROTO_ASSESSMENT_SIZE];
  ble_proto_encode_assessment(a, b);
  enqueue(TX_ASSESSMENT, b, sizeof(b), 0);
}

void ble_gatt_notify_status(void) { enqueue(TX_STATUS, NULL, 0, 0); }

bool ble_gatt_is_connected(void) { return s_conn != BLE_HS_CONN_HANDLE_NONE; }

/* ================================================================ đọc / ghi characteristic */

static int append(struct ble_gatt_access_ctxt *ctxt, const void *data, size_t len) {
  return os_mbuf_append(ctxt->om, data, (uint16_t)len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int read_flat(struct ble_gatt_access_ctxt *ctxt, uint8_t *buf, uint16_t max, uint16_t *len) {
  if (OS_MBUF_PKTLEN(ctxt->om) > max) return BLE_PROTO_ATT_ERR_INVALID;
  return ble_hs_mbuf_to_flat(ctxt->om, buf, max, len) == 0 ? 0 : BLE_ATT_ERR_UNLIKELY;
}

static int read_device_info(struct ble_gatt_access_ctxt *ctxt) {
  wb_config_t c;
  wb_config_get(&c);
  char json[DEVICE_INFO_MAX];
  int n = snprintf(json, sizeof(json), "{\"id\":\"%s\",\"fw\":\"%s\",\"model\":\"%s\",\"cfg\":%u,\"boot\":%u}",
                   device_state_device_id(), esp_app_get_description()->version, SPO2TC_MODEL_VERSION,
                   (unsigned)c.version, (unsigned)device_state_boot_id());
  if (n < 0) return BLE_ATT_ERR_UNLIKELY;
  if (n >= (int)sizeof(json)) n = sizeof(json) - 1;
  return append(ctxt, json, (size_t)n);
}

static int write_control(struct ble_gatt_access_ctxt *ctxt) {
  uint8_t b[BLE_PROTO_CONTROL_SIZE];
  uint16_t len = 0;
  int rc = read_flat(ctxt, b, sizeof(b), &len);
  if (rc) return rc;
  uint8_t op;
  uint32_t arg;
  if (!ble_proto_decode_control(b, len, &op, &arg)) return BLE_PROTO_ATT_ERR_INVALID;

  switch (op) {
    case BLE_OP_ACK:
      record_store_ack(arg);
      ble_gatt_notify_status();   /* đặc tả: notify STATUS sau mỗi ACK */
      return 0;
    case BLE_OP_TIME_SYNC:
      return device_clock_set(arg) ? 0 : BLE_PROTO_ATT_ERR_INVALID;
    case BLE_OP_BACKFILL:
      enqueue(TX_BACKFILL, NULL, 0, arg);
      return 0;
    case BLE_OP_SET_GATEWAY:
      link_manager_on_set_gateway(arg != 0);
      ble_gatt_notify_status();
      return 0;
    default:
      ESP_LOGW(TAG, "CONTROL opcode lạ 0x%02X", op);
      return BLE_PROTO_ATT_ERR_INVALID;
  }
}

static int write_config(struct ble_gatt_access_ctxt *ctxt) {
  uint8_t b[BLE_PROTO_CONFIG_SIZE];
  uint16_t len = 0;
  int rc = read_flat(ctxt, b, sizeof(b), &len);
  if (rc) return rc;
  wb_config_t cur, next;
  wb_config_get(&cur);
  ble_cfg_err_t err = ble_proto_decode_config(b, len, &cur, &next);
  if (err != BLE_CFG_OK) {
    ESP_LOGW(TAG, "CONFIG không hợp lệ (mã %d), giữ config v%u", err, cur.version);
    return BLE_PROTO_ATT_ERR_INVALID;
  }
  if (wb_config_accept(&next)) ble_gatt_notify_status();   /* trùng hoặc cũ hơn: thành công, không làm gì */
  return 0;
}

static int write_wifi(struct ble_gatt_access_ctxt *ctxt) {
  uint8_t b[3 + WIFI_SSID_MAX_LEN + WIFI_PASS_MAX_LEN];
  uint16_t len = 0;
  int rc = read_flat(ctxt, b, sizeof(b), &len);
  if (rc) return rc;
  ble_wifi_cmd_t cmd;
  if (!ble_proto_decode_wifi(b, len, &cmd)) {
    ESP_LOGW(TAG, "WIFI_PROV không hợp lệ (%u byte)", len);
    return BLE_PROTO_ATT_ERR_INVALID;
  }
  if (cmd.op == BLE_WIFI_OP_CLEAR) return wifi_store_clear() == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
  return wifi_store_put(cmd.ssid, cmd.pass) ? 0 : BLE_PROTO_ATT_ERR_INVALID;
}

static int read_wifi(struct ble_gatt_access_ctxt *ctxt) {
  wifi_list_t l;
  wifi_store_get(&l);
  uint8_t b[BLE_PROTO_WIFI_LIST_MAX_SIZE];
  size_t n = ble_proto_encode_wifi_list(&l, b);
  return append(ctxt, b, n);
}

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn;
  (void)attr;
  chr_id_t id = (chr_id_t)(intptr_t)arg;
  bool is_read = ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR;
  bool is_write = ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR;

  if (id == CHR_INFO && is_read) return read_device_info(ctxt);
  if (id == CHR_STATUS && is_read) {
    uint8_t st[BLE_PROTO_STATUS_SIZE];
    build_status(st);
    return append(ctxt, st, sizeof(st));
  }
  if (id == CHR_CONFIG && is_read) {
    wb_config_t c;
    wb_config_get(&c);
    uint8_t b[BLE_PROTO_CONFIG_SIZE];
    ble_proto_encode_config(&c, b);
    return append(ctxt, b, sizeof(b));
  }
  if (id == CHR_CONFIG && is_write) return write_config(ctxt);
  if (id == CHR_CONTROL && is_write) return write_control(ctxt);
  if (id == CHR_WIFI && is_read) return read_wifi(ctxt);
  if (id == CHR_WIFI && is_write) return write_wifi(ctxt);
  return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SVC.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {.uuid = &UUID_INFO.u, .access_cb = chr_access, .arg = (void *)CHR_INFO, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = &UUID_LIVE.u, .access_cb = chr_access, .arg = (void *)CHR_LIVE,
             .flags = BLE_GATT_CHR_F_NOTIFY, .val_handle = &h_live},
            {.uuid = &UUID_RECORDS.u, .access_cb = chr_access, .arg = (void *)CHR_RECORDS,
             .flags = BLE_GATT_CHR_F_NOTIFY, .val_handle = &h_records},
            {.uuid = &UUID_ASSESSMENT.u, .access_cb = chr_access, .arg = (void *)CHR_ASSESSMENT,
             .flags = BLE_GATT_CHR_F_NOTIFY, .val_handle = &h_assessment},
            {.uuid = &UUID_STATUS.u, .access_cb = chr_access, .arg = (void *)CHR_STATUS,
             .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &h_status},
            {.uuid = &UUID_CONTROL.u, .access_cb = chr_access, .arg = (void *)CHR_CONTROL,
             .flags = BLE_GATT_CHR_F_WRITE},
            {.uuid = &UUID_CONFIG.u, .access_cb = chr_access, .arg = (void *)CHR_CONFIG,
             .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE},
            {.uuid = &UUID_WIFI.u, .access_cb = chr_access, .arg = (void *)CHR_WIFI,
             .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE},
            {0},
        },
    },
    {0},
};

/* ================================================================ quảng bá và kết nối */

static void adv_start(bool fast) {
  if (s_conn != BLE_HS_CONN_HANDLE_NONE) return;
  if (ble_gap_adv_active()) ble_gap_adv_stop();

  struct ble_hs_adv_fields f;
  memset(&f, 0, sizeof(f));
  f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  f.uuids128 = &UUID_SVC;   /* app quét theo service UUID */
  f.num_uuids128 = 1;
  f.uuids128_is_complete = 1;
  int rc = ble_gap_adv_set_fields(&f);
  if (rc) {
    ESP_LOGE(TAG, "adv_set_fields lỗi %d", rc);
    return;
  }

  struct ble_hs_adv_fields rsp;   /* tên không đủ chỗ trong gói quảng bá 31 B, đặt ở scan response */
  memset(&rsp, 0, sizeof(rsp));
  rsp.name = (uint8_t *)s_name;
  rsp.name_len = (uint8_t)strlen(s_name);
  rsp.name_is_complete = 1;
  rc = ble_gap_adv_rsp_set_fields(&rsp);
  if (rc) {
    ESP_LOGE(TAG, "adv_rsp_set_fields lỗi %d", rc);
    return;
  }

  struct ble_gap_adv_params p;
  memset(&p, 0, sizeof(p));
  p.conn_mode = BLE_GAP_CONN_MODE_UND;
  p.disc_mode = BLE_GAP_DISC_MODE_GEN;
  p.itvl_min = p.itvl_max = ADV_UNITS(fast ? ADV_FAST_MS : ADV_SLOW_MS);
  rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
  if (rc) {
    ESP_LOGE(TAG, "adv_start lỗi %d", rc);
    return;
  }
  s_adv_fast = fast;
  esp_timer_stop(s_adv_timer);
  if (fast) esp_timer_start_once(s_adv_timer, (uint64_t)CONFIG_RESPI_ADV_FAST_S * 1000000ULL);
  ESP_LOGI(TAG, "quảng bá %s (%d ms)", s_name, fast ? ADV_FAST_MS : ADV_SLOW_MS);
}

static void on_adv_timer(void *arg) {
  (void)arg;
  if (s_conn == BLE_HS_CONN_HANDLE_NONE && s_adv_fast) adv_start(false);
}

static void clear_link(void) {
  s_conn = BLE_HS_CONN_HANDLE_NONE;
  s_mtu = BLE_ATT_MTU_DFLT;
  s_sub_live = s_sub_records = s_sub_assessment = s_sub_status = false;
}

static int gap_event(struct ble_gap_event *ev, void *arg) {
  (void)arg;
  switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (ev->connect.status == 0) {
        clear_link();
        s_conn = ev->connect.conn_handle;
        esp_timer_stop(s_adv_timer);
        ESP_LOGI(TAG, "điện thoại đã kết nối (handle %u)", s_conn);
        link_manager_on_ble_connected();
      } else {
        adv_start(true);
      }
      return 0;

    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(TAG, "mất kết nối (lý do 0x%03X)", ev->disconnect.reason);
      clear_link();
      xQueueReset(s_txq);
      link_manager_on_ble_disconnected();
      adv_start(true);
      return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
      if (s_conn == BLE_HS_CONN_HANDLE_NONE) adv_start(s_adv_fast);
      return 0;

    case BLE_GAP_EVENT_SUBSCRIBE: {
      uint16_t h = ev->subscribe.attr_handle;
      bool on = ev->subscribe.cur_notify;
      if (h == h_live) s_sub_live = on;
      else if (h == h_records) s_sub_records = on;
      else if (h == h_assessment) s_sub_assessment = on;
      else if (h == h_status) s_sub_status = on;
      return 0;
    }

    case BLE_GAP_EVENT_MTU:
      s_mtu = ev->mtu.value;
      ESP_LOGI(TAG, "MTU = %u", s_mtu);
      return 0;

    default:
      return 0;
  }
}

static void on_sync(void) {
  int rc = ble_hs_util_ensure_addr(0);
  if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
  if (rc) {
    ESP_LOGE(TAG, "không lấy được địa chỉ BLE (%d)", rc);
    return;
  }
  adv_start(true);
}

static void on_reset(int reason) { ESP_LOGW(TAG, "NimBLE reset, lý do %d", reason); }

static void host_task(void *param) {
  (void)param;
  nimble_port_run();
  nimble_port_freertos_deinit();
}

void ble_gatt_init(void) {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(s_name, sizeof(s_name), "RespiCare-%02X%02X", mac[4], mac[5]);

  s_txq = xQueueCreate(TXQ_LEN, sizeof(tx_msg_t));
  const esp_timer_create_args_t targs = {.callback = on_adv_timer, .name = "adv_slow"};
  ESP_ERROR_CHECK(esp_timer_create(&targs, &s_adv_timer));

  ESP_ERROR_CHECK(nimble_port_init());
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.reset_cb = on_reset;

  ble_svc_gap_init();
  ble_svc_gatt_init();
  ESP_ERROR_CHECK(ble_gatts_count_cfg(s_svcs) == 0 ? ESP_OK : ESP_FAIL);
  ESP_ERROR_CHECK(ble_gatts_add_svcs(s_svcs) == 0 ? ESP_OK : ESP_FAIL);
  ble_svc_gap_device_name_set(s_name);

  xTaskCreate(tx_task, "ble_tx", 4096, NULL, 5, NULL);
  nimble_port_freertos_init(host_task);
}

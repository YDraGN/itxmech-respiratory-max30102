#include "record_store.h"

#include "device_state.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static const char *TAG = "store";

#define PARTITION_LABEL   "reclog"
#define PARTITION_SUBTYPE 0x40
/* Lưu "seq tối thiểu cho lần boot sau" mỗi khi seq chạm bội số này, phòng khi vùng log bị xóa. */
#define SEQ_FLOOR_STEP    64u

static const esp_partition_t *s_part;
static SemaphoreHandle_t s_mutex;
static uint32_t s_next_seq;
static uint32_t s_acked;

static int part_read(void *ctx, uint32_t off, void *buf, size_t len) {
  return esp_partition_read((const esp_partition_t *)ctx, off, buf, len) == ESP_OK ? 0 : -1;
}
static int part_write(void *ctx, uint32_t off, const void *buf, size_t len) {
  return esp_partition_write((const esp_partition_t *)ctx, off, buf, len) == ESP_OK ? 0 : -1;
}
static int part_erase(void *ctx, uint32_t off) {
  return esp_partition_erase_range((const esp_partition_t *)ctx, off, RLOG_SECTOR_SIZE) == ESP_OK ? 0 : -1;
}

static void lock(void) { xSemaphoreTake(s_mutex, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mutex); }

esp_err_t record_store_init(void) {
  s_mutex = xSemaphoreCreateMutex();
  s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)PARTITION_SUBTYPE,
                                    PARTITION_LABEL);
  if (!s_part) {
    ESP_LOGE(TAG, "không có partition '%s' - kiểm tra partitions.csv", PARTITION_LABEL);
    return ESP_ERR_NOT_FOUND;
  }
  rlog_flash_t flash = {
      .read = part_read, .write = part_write, .erase_sector = part_erase,
      .ctx = (void *)s_part, .size = (uint32_t)(s_part->size / RLOG_SECTOR_SIZE) * RLOG_SECTOR_SIZE,
  };
  if (rlog_open(&flash) != 0) return ESP_FAIL;

  uint32_t floor = device_state_get_u32("seq_floor", 1);
  uint32_t max_seq = rlog_max_seq();
  s_next_seq = (max_seq + 1 > floor) ? max_seq + 1 : floor;
  s_acked = device_state_get_u32("acked_seq", 0);
  if (s_acked >= s_next_seq) s_acked = s_next_seq - 1;

  ESP_LOGI(TAG, "%lu/%lu entry, seq tiếp %lu, acked %lu", (unsigned long)rlog_count(),
           (unsigned long)rlog_capacity(), (unsigned long)s_next_seq, (unsigned long)s_acked);
  return ESP_OK;
}

uint32_t record_store_add_telemetry(ble_record_t *rec) {
  uint8_t payload[BLE_PROTO_RECORD_SIZE];
  lock();
  uint32_t seq = s_next_seq;
  rec->seq = seq;
  ble_proto_encode_record(rec, payload);
  int rc = rlog_append(RLOG_TELEMETRY, seq, device_state_boot_id(), payload, sizeof(payload));
  s_next_seq++;   /* tăng kể cả khi ghi lỗi: không bao giờ dùng lại một seq */
  bool save_floor = (s_next_seq % SEQ_FLOOR_STEP) == 0;
  unlock();

  if (save_floor) device_state_set_u32("seq_floor", s_next_seq + SEQ_FLOOR_STEP);
  if (rc != 0) ESP_LOGE(TAG, "ghi flash bản ghi seq %lu thất bại, chỉ còn gửi trực tiếp", (unsigned long)seq);
  return seq;
}

esp_err_t record_store_add_assessment(const ble_assessment_t *a) {
  uint8_t payload[BLE_PROTO_ASSESSMENT_SIZE];
  ble_proto_encode_assessment(a, payload);
  lock();
  int rc = rlog_append(RLOG_ASSESSMENT, a->seq, device_state_boot_id(), payload, sizeof(payload));
  unlock();
  return rc == 0 ? ESP_OK : ESP_FAIL;
}

uint32_t record_store_last_seq(void) {
  lock();
  uint32_t v = s_next_seq - 1;
  unlock();
  return v;
}

uint32_t record_store_acked_seq(void) {
  lock();
  uint32_t v = s_acked;
  unlock();
  return v;
}

uint16_t record_store_backlog(void) {
  lock();
  uint32_t last = s_next_seq - 1;
  uint32_t n = last > s_acked ? last - s_acked : 0;
  unlock();
  return n > 0xFFFF ? 0xFFFF : (uint16_t)n;
}

bool record_store_ack(uint32_t seq) {
  lock();
  uint32_t last = s_next_seq - 1;
  if (seq > last) seq = last;
  bool advanced = seq > s_acked;
  if (advanced) s_acked = seq;
  unlock();
  if (advanced) device_state_set_u32("acked_seq", seq);
  return advanced;
}

uint32_t record_store_backfill_start(uint32_t requested_from) {
  uint32_t acked = record_store_acked_seq();
  uint32_t keep = CONFIG_RESPI_BACKFILL_KEEP_ACKED;
  uint32_t floor = acked >= keep ? acked - keep + 1 : 1;
  return requested_from > floor ? requested_from : floor;
}

void record_store_cursor_init(rlog_cursor_t *c) {
  lock();
  rlog_cursor_init(c);
  unlock();
}

bool record_store_cursor_next(rlog_cursor_t *c, uint32_t from_seq, uint32_t type_mask, rlog_entry_t *out) {
  lock();
  bool ok = rlog_cursor_next(c, from_seq, type_mask, out);
  unlock();
  return ok;
}

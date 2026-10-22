#include "record_log.h"

#include <string.h>

#define MAGIC   0x31474C52u   /* "RLG1" */
#define OFF_CRC 40u           /* CRC tính trên 40 byte đầu; 8 byte cuối để 0xFF */

static rlog_flash_t s_flash;
static uint32_t s_slots;      /* tổng số slot */
static uint32_t s_head;       /* slot sẽ ghi tiếp */
static uint32_t s_wcount;     /* wcount cho entry ghi tiếp */
static uint32_t s_count;      /* số entry hợp lệ */
static uint32_t s_max_seq, s_min_seq;
static bool s_min_dirty;

/* ---------------------------------------------------------------- tiện ích */

uint32_t rlog_crc32(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFu;
  while (len--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t slot_off(uint32_t slot) {
  return (slot / RLOG_PER_SECTOR) * RLOG_SECTOR_SIZE + (slot % RLOG_PER_SECTOR) * RLOG_ENTRY_SIZE;
}

static bool all_erased(const uint8_t *raw) {
  for (uint32_t i = 0; i < RLOG_ENTRY_SIZE; i++)
    if (raw[i] != 0xFF) return false;
  return true;
}

static void serialize(const rlog_entry_t *e, uint8_t raw[RLOG_ENTRY_SIZE]) {
  memset(raw, 0xFF, RLOG_ENTRY_SIZE);
  put32(raw, MAGIC);
  raw[4] = e->type;
  raw[5] = e->len;
  put16(raw + 6, e->boot_id);
  put32(raw + 8, e->seq);
  put32(raw + 12, e->wcount);
  memset(raw + 16, 0, RLOG_PAYLOAD_MAX);
  memcpy(raw + 16, e->payload, e->len);
  put32(raw + OFF_CRC, rlog_crc32(raw, OFF_CRC));
}

/* 1 = entry hợp lệ, 0 = slot trống (đã xóa), -1 = rác (ghi dở, CRC sai, lỗi đọc). */
static int read_slot(uint32_t slot, rlog_entry_t *e) {
  uint8_t raw[RLOG_ENTRY_SIZE];
  if (s_flash.read(s_flash.ctx, slot_off(slot), raw, sizeof(raw)) != 0) return -1;
  if (all_erased(raw)) return 0;
  if (get32(raw) != MAGIC || get32(raw + OFF_CRC) != rlog_crc32(raw, OFF_CRC)) return -1;
  if ((raw[4] != RLOG_TELEMETRY && raw[4] != RLOG_ASSESSMENT) || raw[5] > RLOG_PAYLOAD_MAX) return -1;
  e->type = raw[4];
  e->len = raw[5];
  e->boot_id = get16(raw + 6);
  e->seq = get32(raw + 8);
  e->wcount = get32(raw + 12);
  memcpy(e->payload, raw + 16, RLOG_PAYLOAD_MAX);
  return 1;
}

static void recompute_min(void) {
  s_min_seq = 0;
  rlog_entry_t e;
  for (uint32_t slot = 0; slot < s_slots; slot++) {
    if (read_slot(slot, &e) == 1 && e.type == RLOG_TELEMETRY && (s_min_seq == 0 || e.seq < s_min_seq))
      s_min_seq = e.seq;
  }
  s_min_dirty = false;
}

/* ---------------------------------------------------------------- API */

int rlog_open(const rlog_flash_t *flash) {
  if (!flash || flash->size < 2 * RLOG_SECTOR_SIZE || flash->size % RLOG_SECTOR_SIZE) return -1;
  s_flash = *flash;
  s_slots = (flash->size / RLOG_SECTOR_SIZE) * RLOG_PER_SECTOR;
  s_head = s_wcount = s_count = s_max_seq = s_min_seq = 0;
  s_min_dirty = false;

  bool found = false;
  uint32_t best_slot = 0, best_w = 0;
  rlog_entry_t e;
  for (uint32_t slot = 0; slot < s_slots; slot++) {
    if (read_slot(slot, &e) != 1) continue;
    s_count++;
    if (!found || e.wcount > best_w) {
      found = true;
      best_w = e.wcount;
      best_slot = slot;
    }
    if (e.type == RLOG_TELEMETRY) {
      if (e.seq > s_max_seq) s_max_seq = e.seq;
      if (s_min_seq == 0 || e.seq < s_min_seq) s_min_seq = e.seq;
    }
  }
  if (found) {
    s_head = (best_slot + 1) % s_slots;
    s_wcount = best_w + 1;
  }
  /* Trong sector đang ghi dở chỉ được ghi vào slot còn trống: bỏ qua slot rác do mất điện. */
  uint8_t raw[RLOG_ENTRY_SIZE];
  while (s_head % RLOG_PER_SECTOR != 0) {
    if (s_flash.read(s_flash.ctx, slot_off(s_head), raw, sizeof(raw)) == 0 && all_erased(raw)) break;
    s_head = (s_head + 1) % s_slots;
  }
  return 0;
}

int rlog_append(rlog_type_t type, uint32_t seq, uint16_t boot_id, const void *payload, size_t len) {
  if (s_slots == 0 || len > RLOG_PAYLOAD_MAX) return -1;

  if (s_head % RLOG_PER_SECTOR == 0) {
    uint32_t lost = 0;
    rlog_entry_t old;
    for (uint32_t i = 0; i < RLOG_PER_SECTOR; i++)
      if (read_slot(s_head + i, &old) == 1) lost++;
    if (s_flash.erase_sector(s_flash.ctx, slot_off(s_head)) != 0) return -1;
    s_count = s_count > lost ? s_count - lost : 0;
    if (lost) s_min_dirty = true;
  }

  rlog_entry_t e = {
      .type = (uint8_t)type, .len = (uint8_t)len, .boot_id = boot_id, .seq = seq, .wcount = s_wcount,
  };
  memcpy(e.payload, payload, len);
  uint8_t raw[RLOG_ENTRY_SIZE];
  serialize(&e, raw);
  int rc = s_flash.write(s_flash.ctx, slot_off(s_head), raw, sizeof(raw));

  /* Kể cả khi ghi lỗi vẫn tiến đầu ghi: slot đó có thể đã bẩn, không ghi đè lên được. */
  s_head = (s_head + 1) % s_slots;
  s_wcount++;
  if (rc != 0) return -1;

  s_count++;
  if (type == RLOG_TELEMETRY) {
    if (seq > s_max_seq) s_max_seq = seq;
    if (s_min_seq == 0) s_min_seq = seq;
  }
  return 0;
}

uint32_t rlog_capacity(void) { return s_slots; }
uint32_t rlog_count(void) { return s_count; }
uint32_t rlog_max_seq(void) { return s_max_seq; }

uint32_t rlog_min_seq(void) {
  if (s_min_dirty) recompute_min();
  return s_min_seq;
}

void rlog_cursor_init(rlog_cursor_t *c) {
  c->idx = s_head;   /* sau đầu ghi là vùng cũ nhất */
  c->visited = 0;
}

bool rlog_cursor_next(rlog_cursor_t *c, uint32_t from_seq, uint32_t type_mask, rlog_entry_t *out) {
  while (c->visited < s_slots) {
    uint32_t slot = c->idx;
    if (c->visited > 0 && slot == s_head) return false;   /* đã đuổi kịp đầu ghi */
    c->idx = (c->idx + 1) % s_slots;
    c->visited++;
    rlog_entry_t e;
    if (read_slot(slot, &e) != 1) continue;
    if (!(type_mask & RLOG_MASK(e.type)) || e.seq < from_seq) continue;
    *out = e;
    return true;
  }
  return false;
}

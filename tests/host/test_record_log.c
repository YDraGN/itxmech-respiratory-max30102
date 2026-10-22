/* Kiểm tra bộ đệm vòng trên flash giả lập NOR (ghi chỉ xóa bit 1 → 0, xóa theo sector). */
#include <stdlib.h>
#include <string.h>

#include "record_log.h"
#include "test_util.h"

#define SECTORS 3
static uint8_t g_flash[SECTORS * RLOG_SECTOR_SIZE];

static int fl_read(void *ctx, uint32_t off, void *buf, size_t len) {
  (void)ctx;
  if (off + len > sizeof(g_flash)) return -1;
  memcpy(buf, g_flash + off, len);
  return 0;
}
static int fl_write(void *ctx, uint32_t off, const void *buf, size_t len) {
  (void)ctx;
  if (off + len > sizeof(g_flash)) return -1;
  const uint8_t *p = buf;
  for (size_t i = 0; i < len; i++) g_flash[off + i] &= p[i];
  return 0;
}
static int fl_erase(void *ctx, uint32_t off) {
  (void)ctx;
  if (off % RLOG_SECTOR_SIZE) return -1;
  memset(g_flash + off, 0xFF, RLOG_SECTOR_SIZE);
  return 0;
}

static const rlog_flash_t FLASH = {fl_read, fl_write, fl_erase, NULL, sizeof(g_flash)};

static void reboot(void) { CHECK(rlog_open(&FLASH) == 0); }

static void add_tel(uint32_t seq) {
  uint8_t p[16];
  memset(p, (int)(seq & 0xFF), sizeof(p));
  CHECK(rlog_append(RLOG_TELEMETRY, seq, 1, p, sizeof(p)) == 0);
}

/* Đếm và kiểm tra seq telemetry tăng liên tục từ from đến hết. */
static uint32_t walk(uint32_t from, uint32_t *first, uint32_t *last) {
  rlog_cursor_t c;
  rlog_entry_t e;
  uint32_t n = 0, prev = 0;
  rlog_cursor_init(&c);
  while (rlog_cursor_next(&c, from, RLOG_MASK(RLOG_TELEMETRY), &e)) {
    if (n == 0) *first = e.seq;
    else CHECK(e.seq == prev + 1);
    CHECK(e.payload[0] == (uint8_t)(e.seq & 0xFF));
    prev = e.seq;
    n++;
  }
  *last = prev;
  return n;
}

static void test_empty_and_reboot(void) {
  memset(g_flash, 0xFF, sizeof(g_flash));
  reboot();
  CHECK(rlog_count() == 0 && rlog_max_seq() == 0);
  CHECK(rlog_capacity() == SECTORS * RLOG_PER_SECTOR);

  for (uint32_t s = 1; s <= 100; s++) add_tel(s);
  CHECK(rlog_count() == 100 && rlog_max_seq() == 100 && rlog_min_seq() == 1);

  reboot();   /* khởi động lại giữa sector thứ hai */
  CHECK(rlog_count() == 100 && rlog_max_seq() == 100);
  add_tel(101);
  uint32_t first, last;
  CHECK(walk(0, &first, &last) == 101 && first == 1 && last == 101);
  CHECK(walk(95, &first, &last) == 7 && first == 95);
}

static void test_wraparound(void) {
  memset(g_flash, 0xFF, sizeof(g_flash));
  reboot();
  for (uint32_t s = 1; s <= 1000; s++) add_tel(s);
  uint32_t cap = rlog_capacity();
  CHECK(rlog_count() <= cap && rlog_count() >= cap - RLOG_PER_SECTOR);
  CHECK(rlog_max_seq() == 1000);
  uint32_t first, last;
  uint32_t n = walk(0, &first, &last);
  CHECK(n == rlog_count() && last == 1000 && first == rlog_min_seq());

  reboot();   /* sau khi quay vòng vẫn tìm đúng đầu ghi */
  add_tel(1001);
  CHECK(walk(0, &first, &last) == rlog_count() && last == 1001);
}

static void test_power_loss_mid_write(void) {
  memset(g_flash, 0xFF, sizeof(g_flash));
  reboot();
  for (uint32_t s = 1; s <= 10; s++) add_tel(s);
  /* Giả lập mất điện khi đang ghi slot 10: chỉ vài byte đầu kịp ghi. */
  uint32_t off = 10 * RLOG_ENTRY_SIZE;
  g_flash[off] = 0x52;
  g_flash[off + 1] = 0x4C;
  reboot();
  CHECK(rlog_count() == 10);
  add_tel(11);   /* phải bỏ qua slot rác, ghi vào slot 11 */
  uint32_t first, last;
  CHECK(walk(0, &first, &last) == 11 && last == 11);
  reboot();
  CHECK(rlog_count() == 11 && rlog_max_seq() == 11);
}

static void test_mixed_types(void) {
  memset(g_flash, 0xFF, sizeof(g_flash));
  reboot();
  uint8_t a[24] = {0};
  for (uint32_t s = 1; s <= 20; s++) {
    add_tel(s);
    if (s % 5 == 0) CHECK(rlog_append(RLOG_ASSESSMENT, s, 1, a, sizeof(a)) == 0);
  }
  rlog_cursor_t c;
  rlog_entry_t e;
  int n_asm = 0, n_all = 0;
  rlog_cursor_init(&c);
  while (rlog_cursor_next(&c, 10, RLOG_MASK(RLOG_ASSESSMENT), &e)) {
    CHECK(e.type == RLOG_ASSESSMENT && e.seq >= 10 && e.len == 24);
    n_asm++;
  }
  CHECK(n_asm == 3);   /* seq 10, 15, 20 */
  rlog_cursor_init(&c);
  while (rlog_cursor_next(&c, 0, RLOG_MASK(RLOG_TELEMETRY) | RLOG_MASK(RLOG_ASSESSMENT), &e)) n_all++;
  CHECK(n_all == 24);
  CHECK(rlog_max_seq() == 20);   /* chỉ tính telemetry */
}

static void test_dirty_partition(void) {
  /* Partition cũ chứa dữ liệu lạ: không coi là entry hợp lệ, ghi đè dần được. */
  for (size_t i = 0; i < sizeof(g_flash); i++) g_flash[i] = (uint8_t)rand();
  reboot();
  CHECK(rlog_count() == 0);
  for (uint32_t s = 1; s <= 5; s++) add_tel(s);
  uint32_t first, last;
  CHECK(walk(0, &first, &last) == 5 && first == 1 && last == 5);
}

int main(void) {
  test_empty_and_reboot();
  test_wraparound();
  test_power_loss_mid_write();
  test_mixed_types();
  test_dirty_partition();
  return test_report("record_log");
}

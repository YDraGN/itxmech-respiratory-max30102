#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bộ đệm vòng trên flash cho bản ghi phút (telemetry) và assessment.
 *
 * - Mỗi entry 48 B, 85 entry mỗi sector 4 KB. Ghi nối tiếp; sang sector mới thì xóa sector đó
 *   (mất 85 entry cũ nhất). Không xóa theo ACK: ACK chỉ là con trỏ, xem record_store.h.
 * - Mỗi entry có CRC32, entry ghi dở do mất điện bị bỏ qua lúc đọc.
 * - Thứ tự ghi theo wcount (tăng dần, không phụ thuộc seq), nên tìm lại đầu ghi sau khi boot.
 * - Không khóa: bên gọi tự đảm bảo chỉ một task dùng tại một thời điểm.
 * - Không phụ thuộc ESP-IDF; truy cập flash qua rlog_flash_t để test được trên máy tính.
 */

#define RLOG_SECTOR_SIZE   4096u
#define RLOG_ENTRY_SIZE    48u
#define RLOG_PER_SECTOR    (RLOG_SECTOR_SIZE / RLOG_ENTRY_SIZE)   /* 85 */
#define RLOG_PAYLOAD_MAX   24u

typedef enum { RLOG_TELEMETRY = 1, RLOG_ASSESSMENT = 2 } rlog_type_t;
#define RLOG_MASK(t) (1u << (t))

typedef struct {
  uint8_t type;
  uint8_t len;
  uint16_t boot_id;
  uint32_t seq;
  uint32_t wcount;
  uint8_t payload[RLOG_PAYLOAD_MAX];
} rlog_entry_t;

typedef struct {
  int (*read)(void *ctx, uint32_t off, void *buf, size_t len);         /* 0 = OK */
  int (*write)(void *ctx, uint32_t off, const void *buf, size_t len);
  int (*erase_sector)(void *ctx, uint32_t off);
  void *ctx;
  uint32_t size;   /* byte, bội số của RLOG_SECTOR_SIZE, ít nhất 2 sector */
} rlog_flash_t;

typedef struct {
  uint32_t idx;       /* slot sẽ đọc tiếp */
  uint32_t visited;   /* số slot đã đi qua, dừng khi đủ một vòng */
} rlog_cursor_t;

/* Quét toàn bộ vùng nhớ, tìm đầu ghi. 0 = OK. */
int rlog_open(const rlog_flash_t *flash);

int rlog_append(rlog_type_t type, uint32_t seq, uint16_t boot_id, const void *payload, size_t len);

uint32_t rlog_capacity(void);
uint32_t rlog_count(void);
/* seq telemetry lớn nhất / nhỏ nhất còn trong flash (0 nếu chưa có). */
uint32_t rlog_max_seq(void);
uint32_t rlog_min_seq(void);

/* Duyệt từ cũ đến mới. Mỗi lần gọi next trả entry kế tiếp có seq >= from_seq và type thuộc mask. */
void rlog_cursor_init(rlog_cursor_t *c);
bool rlog_cursor_next(rlog_cursor_t *c, uint32_t from_seq, uint32_t type_mask, rlog_entry_t *out);

/* CRC32 (IEEE, giống zlib). */
uint32_t rlog_crc32(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

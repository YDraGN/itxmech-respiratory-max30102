#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ble_proto.h"
#include "spo2_tc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Gộp từng giây thành bản ghi phút và giữ lịch sử ngắn cho ASSESSMENT. */

typedef struct {
  float m[SPO2TC_N_CH];   /* đầu vào engine: NAN = thiếu, spo2_valid không bao giờ NAN */
  uint32_t ts_start;      /* device_clock_now() lúc bắt đầu phút */
  bool worn;              /* quá nửa số giây có tiếp xúc */
  uint8_t sqi_pct;        /* tạm tính = tỷ lệ mẫu có tiếp xúc trong phút */
} vitals_minute_t;

void vitals_init(void);

/* Gọi mỗi giây từ task cảm biến (an toàn giữa các task). */
void vitals_add_second(bool valid, float spo2, float hr, bool contact, uint8_t sqi_pct);

/* Kết thúc phút hiện tại, bắt đầu phút mới. */
void vitals_finish_minute(vitals_minute_t *out);

/*
 * Kết thúc cửa sổ hiển thị (LIVE, mặc định 15 s), bắt đầu cửa sổ mới. Trả số giây đã gộp.
 * Cùng cách gộp như bản ghi phút (trung bình / nhỏ nhất các giây hợp lệ), nên số trên app, máy người nhà
 * và web là một, chỉ khác độ dài cửa sổ.
 */
int vitals_finish_window(ble_live_t *out);

/* Lưu phút vào lịch sử 30 phút (sau khi đã push engine). */
void vitals_history_push(const float m[SPO2TC_N_CH]);
float vitals_spo2_min_10m(void);     /* NAN nếu không có phút hợp lệ */
float vitals_spo2_slope_30m(void);   /* %/phút, NAN nếu ít hơn 10 phút hợp lệ */

/* Bitmask reasons của ASSESSMENT (ble_proto.h). */
uint16_t vitals_reasons(const vitals_minute_t *vm, const spo2tc_config_t *cfg, float slope_30m);

/* Trạng thái đeo hiện tại (giây gần nhất), dùng cho STATUS. */
bool vitals_is_worn(void);

#ifdef __cplusplus
}
#endif

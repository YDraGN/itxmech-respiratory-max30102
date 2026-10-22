#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mốc epoch nhỏ nhất coi là "giờ thật". Giá trị ts nhỏ hơn là số giây kể từ lúc boot. */
#define CLOCK_MIN_VALID_EPOCH 1600000000u

/* Đặt giờ từ TIME_SYNC (app) hoặc server_time (uplink). Bỏ qua giá trị vô lý. */
bool device_clock_set(uint32_t epoch);
bool device_clock_is_synced(void);

/* Epoch giây UTC nếu đã đồng bộ, ngược lại số giây kể từ lúc boot (< CLOCK_MIN_VALID_EPOCH). */
uint32_t device_clock_now(void);

/* Đổi một ts ghi lúc chưa đồng bộ (cùng lần boot) sang epoch. Trả lại nguyên nếu không đổi được. */
uint32_t device_clock_fix_ts(uint32_t ts);

#ifdef __cplusplus
}
#endif

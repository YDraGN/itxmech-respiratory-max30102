#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Xử lý tín hiệu PPG từ MAX30102: thuật toán giữ nguyên bản phần cứng đã chạy thử
 * (ngưỡng tiếp xúc IR > 40000, SpO2 = 104 - 17R trên cửa sổ 100 mẫu, bắt đỉnh IR để tính BPM).
 * Chỉ gọi từ một task (task cảm biến).
 */

/* Xử lý một lô mẫu đọc từ FIFO. */
void ppg_process(const uint32_t *red, const uint32_t *ir, int n);

float ppg_last_spo2(void);   /* 0 nếu chưa có / mất tiếp xúc */
float ppg_last_hr(void);     /* 0 nếu chưa có */
bool ppg_contact_now(void);  /* mẫu cuối có tiếp xúc (IR > 40000): đang đeo / có tay */

/* Thống kê từ lần gọi trước: tổng số mẫu và số mẫu có tiếp xúc. Gọi mỗi giây. */
void ppg_take_counts(int *total, int *with_contact);

#ifdef __cplusplus
}
#endif

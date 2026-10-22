#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t max30102_init(void);

/**
 * Đọc tất cả mẫu đang có trong FIFO.
 * @param red  mảng nhận giá trị RED (18-bit)
 * @param ir   mảng nhận giá trị IR  (18-bit)
 * @param max_n tối đa số mẫu muốn lấy (thường 32)
 * @return số mẫu thực tế đọc được
 */
int max30102_read(uint32_t *red, uint32_t *ir, int max_n);

#ifdef __cplusplus
}
#endif
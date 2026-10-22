#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "spo2_tc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Config bác sĩ đặt, đúng device_cfg_t ở SPO2-RESEARCH/.../firmware/docs/CONFIG_VA_API.md §4.1. */
typedef struct {
  uint16_t version;              /* 0 = config xuất xưởng */
  bool ml_enabled;
  bool model_ok;                 /* ngưỡng INT8 gửi xuống khớp bảng của firmware */
  uint8_t upload_interval_min;   /* chu kỳ gửi lô khi chạy WiFi */
  spo2tc_config_t eng;
} wb_config_t;

/* Config xuất xưởng (§4.4). Inline để test trên máy tính dùng được. */
static inline void wb_config_set_default(wb_config_t *c) {
  c->version = 0;
  c->ml_enabled = true;
  c->model_ok = true;
  c->upload_interval_min = 10;
  spo2tc_config_default(&c->eng);
}

/* Đọc config đã lưu (NVS); hỏng hoặc chưa có thì dùng config xuất xưởng. */
void wb_config_init(void);

/* Bản sao config đang dùng (an toàn giữa các task). */
void wb_config_get(wb_config_t *out);

/*
 * Nhận config mới đã kiểm tra hợp lệ (từ BLE CONFIG hoặc server). Lưu NVS và cập nhật
 * version ngay để app thấy; engine chỉ được đổi trong task phút qua wb_config_take_pending().
 * Trả true nếu version mới hơn và đã nhận.
 */
bool wb_config_accept(const wb_config_t *c);

/* Task phút gọi trước mỗi lần push: true nếu có config mới cần áp vào engine. */
bool wb_config_take_pending(wb_config_t *out);

#ifdef __cplusplus
}
#endif

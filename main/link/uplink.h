#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Chế độ WiFi: gửi thẳng lên server khi không có app (trạng thái STANDALONE).
 *
 * Mỗi upload_interval_min phút (hoặc ngay khi có ALERT/CRITICAL): bật WiFi (thử các mạng app đã cài,
 * mới nhất trước), POST /api/app/v1/ingest/assessments + ingest/telemetry với header x-device-id /
 * x-device-key (khóa app ghi qua BLE, device_cred.h), lấy giờ từ server_time, lấy ngưỡng mới từ
 * GET device/config, rồi tắt WiFi. Chưa có WiFi hoặc khóa thì dữ liệu nằm trong flash, chờ app.
 */
void uplink_init(void);
void uplink_set_active(bool active);
void uplink_kick(bool urgent);
bool uplink_wifi_on(void);

/* Gọi sau khi danh sách WiFi hoặc khóa đổi: cập nhật LED "chế độ WiFi". */
void uplink_refresh_ready(void);

/* Có ít nhất một WiFi và khóa thiết bị (đọc lại sau mỗi lần app ghi WIFI_PROV). */
bool uplink_wifi_ready(void);

#ifdef __cplusplus
}
#endif

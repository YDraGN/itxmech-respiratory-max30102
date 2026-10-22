#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Đường gửi thẳng lên server khi không có app (chế độ STANDALONE).
 *
 * HIỆN CHƯA GỬI MẠNG: phần WiFi + HTTPS (POST /api/app/v1/ingest/..., CONFIG_VA_API.md §6)
 * sẽ làm sau khi server có endpoint và khóa thiết bị (xem README, mục "Việc còn lại").
 * Danh sách WiFi app gửi xuống đã có sẵn trong wifi_store_get().
 * Task này đã chạy đúng nhịp (mỗi upload_interval_min, hoặc ngay khi có cảnh báo) và chỉ
 * ghi log lượng dữ liệu đang chờ, để khi thêm HTTP chỉ cần điền vào uplink_flush().
 * Dữ liệu vẫn nằm an toàn trong flash cho tới khi app hoặc uplink lấy đi.
 */
void uplink_init(void);
void uplink_set_active(bool active);
void uplink_kick(bool urgent);
bool uplink_wifi_on(void);

#ifdef __cplusplus
}
#endif

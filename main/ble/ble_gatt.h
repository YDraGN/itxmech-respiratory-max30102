#pragma once
#include <stdbool.h>
#include "ble_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GATT server theo đặc tả BLE v1 (service a51c0001-7b2e-4f6a-9c3d-5e8f1b2a6c40).
 * Mỗi lúc một kết nối. Mọi notify đi qua một task gửi riêng nên các hàm send_* không chặn;
 * LIVE bị bỏ nếu hàng đợi đầy, RECORDS/ASSESSMENT luôn còn trong flash để BACKFILL.
 */
void ble_gatt_init(void);

bool ble_gatt_is_connected(void);

void ble_gatt_send_live(const ble_live_t *v);
void ble_gatt_send_record(const ble_record_t *r);
void ble_gatt_send_assessment(const ble_assessment_t *a);
void ble_gatt_notify_status(void);

#ifdef __cplusplus
}
#endif

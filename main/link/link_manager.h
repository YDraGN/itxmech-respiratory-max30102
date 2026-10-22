#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Máy trạng thái đường truyền (docs/architecture.md):
 *
 *   WAITING_APP ──SET_GATEWAY(1)──► GATEWAY ──mất BLE──► WAITING_APP
 *        │                             │
 *        │ hết CONFIG_RESPI_APP_WAIT_S │ SET_GATEWAY(0)
 *        ▼                             ▼
 *   STANDALONE ◄───────────────────────┘      (STANDALONE ──SET_GATEWAY(1)──► GATEWAY)
 *
 * BLE luôn quảng bá khi không có kết nối, kể cả ở STANDALONE, để app lấy lại quyền gateway.
 * Dữ liệu luôn được ghi flash trước; trạng thái chỉ quyết định ai gửi đi.
 */
typedef enum { LINK_WAITING_APP = 0, LINK_GATEWAY, LINK_STANDALONE } link_state_t;

void link_manager_init(void);

void link_manager_on_ble_connected(void);
void link_manager_on_ble_disconnected(void);
void link_manager_on_set_gateway(bool on);

/* Có ALERT/CRITICAL mới: ở STANDALONE thì báo uplink gửi ngay. */
void link_manager_on_urgent(void);

link_state_t link_manager_state(void);
const char *link_manager_state_name(link_state_t s);

#ifdef __cplusplus
}
#endif

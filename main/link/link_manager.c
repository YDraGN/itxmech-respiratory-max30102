#include "link_manager.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "led_status.h"
#include "sdkconfig.h"
#include "uplink.h"

static const char *TAG = "link";

static SemaphoreHandle_t s_mutex;
static esp_timer_handle_t s_wait_timer;
static link_state_t s_state = LINK_WAITING_APP;

const char *link_manager_state_name(link_state_t s) {
  switch (s) {
    case LINK_WAITING_APP: return "WAITING_APP";
    case LINK_GATEWAY: return "GATEWAY";
    case LINK_STANDALONE: return "STANDALONE";
  }
  return "?";
}

/* Gọi khi đang giữ s_mutex. Mọi hiệu ứng phụ của một trạng thái nằm ở đây. */
static void enter(link_state_t next) {
  if (next == s_state) return;
  ESP_LOGI(TAG, "%s -> %s", link_manager_state_name(s_state), link_manager_state_name(next));
  s_state = next;
  esp_timer_stop(s_wait_timer);   /* trả lỗi nếu timer không chạy: bỏ qua */

  switch (next) {
    case LINK_WAITING_APP:
      uplink_set_active(false);
      led_status_searching(CONFIG_RESPI_LED_SEARCH_S);
      esp_timer_start_once(s_wait_timer, (uint64_t)CONFIG_RESPI_APP_WAIT_S * 1000000ULL);
      break;
    case LINK_GATEWAY:
      uplink_set_active(false);
      led_status_connected();
      break;
    case LINK_STANDALONE:
      uplink_set_active(true);
      break;
  }
}

static void on_wait_timeout(void *arg) {
  (void)arg;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_state == LINK_WAITING_APP) {
    ESP_LOGW(TAG, "không có app sau %d s, vòng tự lưu dữ liệu", CONFIG_RESPI_APP_WAIT_S);
    enter(LINK_STANDALONE);
  }
  xSemaphoreGive(s_mutex);
}

void link_manager_init(void) {
  s_mutex = xSemaphoreCreateMutex();
  const esp_timer_create_args_t args = {.callback = on_wait_timeout, .name = "link_wait"};
  ESP_ERROR_CHECK(esp_timer_create(&args, &s_wait_timer));
  /* Khởi động như vừa mất kết nối: chờ app, LED nháy nhanh. */
  s_state = LINK_STANDALONE;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  enter(LINK_WAITING_APP);
  xSemaphoreGive(s_mutex);
}

void link_manager_on_ble_connected(void) {
  /* Chưa đổi trạng thái: chỉ coi là có app khi app bắt tay xong bằng SET_GATEWAY(1). */
  ESP_LOGI(TAG, "BLE đã kết nối, chờ app bắt tay");
}

void link_manager_on_ble_disconnected(void) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  /* App chủ động trả quyền (SET_GATEWAY(0)) thì đã ở STANDALONE, giữ nguyên. */
  if (s_state == LINK_GATEWAY) enter(LINK_WAITING_APP);
  xSemaphoreGive(s_mutex);
}

void link_manager_on_set_gateway(bool on) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  enter(on ? LINK_GATEWAY : LINK_STANDALONE);
  xSemaphoreGive(s_mutex);
}

void link_manager_on_urgent(void) {
  if (link_manager_state() == LINK_STANDALONE) uplink_kick(true);
}

link_state_t link_manager_state(void) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  link_state_t s = s_state;
  xSemaphoreGive(s_mutex);
  return s;
}

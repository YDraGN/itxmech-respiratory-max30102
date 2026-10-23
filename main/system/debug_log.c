#include "debug_log.h"

#include <stdarg.h>
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"

#define BUF_SIZE  4096
#define LINE_MAX  128   /* dòng dài hơn bị cắt; giữ nhỏ vì hàm chạy trên stack của task đang log */

static StreamBufferHandle_t s_sb;
static SemaphoreHandle_t s_lock;
static vprintf_like_t s_uart;
static volatile bool s_on;

static int hook(const char *fmt, va_list ap) {
  va_list copy;
  va_copy(copy, ap);
  int n = s_uart ? s_uart(fmt, ap) : 0;
  if (s_on && s_sb) {
    char line[LINE_MAX];
    int len = vsnprintf(line, sizeof(line), fmt, copy);
    if (len > 0) {
      if (len >= (int)sizeof(line)) {
        len = sizeof(line) - 1;
        line[len - 1] = '\n';
      }
      /* Stream buffer chỉ cho một bên ghi: khóa thử, bận hoặc đầy thì bỏ dòng, không bao giờ chặn task đang log. */
      if (xSemaphoreTake(s_lock, 0) == pdTRUE) {
        xStreamBufferSend(s_sb, line, (size_t)len, 0);
        xSemaphoreGive(s_lock);
      }
    }
  }
  va_end(copy);
  return n;
}

void debug_log_init(void) {
  s_lock = xSemaphoreCreateMutex();
  s_sb = xStreamBufferCreate(BUF_SIZE, 1);
  if (s_sb && s_lock) s_uart = esp_log_set_vprintf(hook);
}

/* Tắt thì phần còn trong bộ đệm vẫn được task ble_log đọc ra rồi bỏ (không ai nghe). */
void debug_log_set_enabled(bool on) { s_on = on; }

size_t debug_log_read(char *out, size_t max, unsigned wait_ms) {
  if (!s_sb) return 0;
  return xStreamBufferReceive(s_sb, out, max, pdMS_TO_TICKS(wait_ms));
}

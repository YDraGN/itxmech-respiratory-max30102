#include "max30102.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define PIN_SDA     8
#define PIN_SCL     9
#define MAX_ADDR    0x57

static const char *TAG = "max30102";
static i2c_master_dev_handle_t s_dev = NULL;

static esp_err_t wr(uint8_t reg, uint8_t v)
{
    uint8_t buf[2] = { reg, v };
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

static esp_err_t rd(uint8_t reg, uint8_t *v, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, v, n, 100);
}

esp_err_t max30102_init(void)
{
    // --- Bus I2C ---
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX_ADDR,
        .scl_speed_hz = 400000,          // Fast Mode 400 kHz
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev));

    // --- Kiểm tra PART_ID ---
    uint8_t id = 0;
    if (rd(0xFF, &id, 1) != ESP_OK || id != 0x15) {
        ESP_LOGE(TAG, "PART_ID = 0x%02X (mong đợi 0x15) – kiểm tra dây / nguồn", id);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "MAX30102 detected (PART_ID=0x15)");

    // --- Cấu hình thanh ghi (giống code Arduino của bạn) ---
    wr(0x09, 0x40);                      // Reset
    vTaskDelay(pdMS_TO_TICKS(10));

    wr(0x04, 0x00);                      // FIFO_WR_PTR = 0
    wr(0x05, 0x00);                      // OVF_COUNTER = 0
    wr(0x06, 0x00);                      // FIFO_RD_PTR = 0

    wr(0x08, 0x5F);                      // FIFO: Sample Avg = 4, Rollover enable
    wr(0x0A, 0x27);                      // SpO2: 4096 nA, 100 sps, 411 µs (18-bit)

    // Dòng LED ~11 mA (0x36) – phù hợp cổ tay, có thể chỉnh sau
    wr(0x0C, 0x36);                      // RED LED
    wr(0x0D, 0x36);                      // IR  LED

    return wr(0x09, 0x03);               // Mode = SpO2 (RED + IR)
}

int max30102_read(uint32_t *red, uint32_t *ir, int max_n)
{
    uint8_t wr_ptr = 0, rd_ptr = 0;
    if (rd(0x04, &wr_ptr, 1) != ESP_OK) return 0;
    if (rd(0x06, &rd_ptr, 1) != ESP_OK) return 0;

    int n = (wr_ptr - rd_ptr) & 0x1F;
    if (n > max_n) n = max_n;
    if (n <= 0) return 0;

    for (int i = 0; i < n; i++) {
        uint8_t b[6];
        if (rd(0x07, b, 6) != ESP_OK) {
            return i;                    // trả về số mẫu đã đọc được
        }
        red[i] = (((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2]) & 0x3FFFF;
        ir[i]  = (((uint32_t)b[3] << 16) | ((uint32_t)b[4] << 8) | b[5]) & 0x3FFFF;
    }
    return n;
}
// main.cc - hybrid_tc trên ESP32-C3 SuperMini + MAX30102
// - Self-test 24/24 lúc boot
// - Đọc MAX30102 theo sơ đồ: SDA=8, SCL=9, INT=10, RLED=4, GLED=5
// - Xử lý mỗi giây → gộp phút → spo2tc_engine_push_minute

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "model_runner.h"
#include "spo2_tc.h"
#include "golden_vectors.h"
#include "max30102.h"          // driver bước 4

static const char *TAG = "spo2tc";

// ===================== Chân theo sơ đồ của bạn =====================
#define RLED_PIN GPIO_NUM_4
#define GLED_PIN GPIO_NUM_5
#define INT_PIN  GPIO_NUM_10

// ===================== Self-test (giữ nguyên) =====================
static int self_test(void) {
    int ok = 0;
    int64_t t0 = esp_timer_get_time();
    for (int g = 0; g < SPO2TC_N_GOLDEN; g++) {
        const spo2tc_golden_t *v = &SPO2TC_GOLDEN[g];
        const float(*x)[SPO2TC_N_CH] = (const float(*)[SPO2TC_N_CH])v->x;
        float tab[SPO2TC_N_TAB];
        int8_t tq[SPO2TC_N_TAB], sq[SPO2TC_IN_MIN * SPO2TC_N_SEQ_CH];
        spo2tc_features(x, v->T, v->baseline, v->base_hours, tab);
        spo2tc_quantize_tab(tab, tq);
        spo2tc_quantize_seq(x, v->T, sq);
        int8_t q = model_runner_invoke(tq, sq, NULL);
        if (q == v->out_q) ok++;
        else ESP_LOGW(TAG, "vector %d: q_out %d, mong đợi %d", g, q, v->out_q);
    }
    int64_t us = (esp_timer_get_time() - t0) / SPO2TC_N_GOLDEN;
    ESP_LOGI(TAG, "self-test: %d/%d khớp | trung bình %lld us/lần", ok, SPO2TC_N_GOLDEN, us);
    return (ok == SPO2TC_N_GOLDEN) ? 0 : -1;
}

// ===================== Bộ gộp phút =====================
typedef struct {
    double sp_sum, pu_sum;
    float  sp_min;
    int    n_sp, n_pu, n_sec;
} minute_acc_t;

static minute_acc_t s_acc;
static SemaphoreHandle_t s_acc_mutex;

static void acc_reset(minute_acc_t *a) {
    a->sp_sum = a->pu_sum = 0.0;
    a->sp_min = INFINITY;
    a->n_sp = a->n_pu = a->n_sec = 0;
}

static void acc_add_second(minute_acc_t *a, int valid, float spo2, float hr) {
    a->n_sec++;
    if (!valid) return;                     // giây kém → bỏ, KHÔNG ghi 0
    a->sp_sum += spo2;
    a->n_sp++;
    if (spo2 < a->sp_min) a->sp_min = spo2;
    if (isfinite(hr) && hr > 0) {
        a->pu_sum += hr;
        a->n_pu++;
    }
}

static void acc_finish(const minute_acc_t *a, float m[SPO2TC_N_CH]) {
    m[SPO2TC_SPO2_MEAN]  = a->n_sp ? (float)(a->sp_sum / a->n_sp) : NAN;
    m[SPO2TC_SPO2_MIN]   = a->n_sp ? a->sp_min : NAN;
    m[SPO2TC_SPO2_VALID] = a->n_sp / 60.0f;          // không bao giờ NAN
    m[SPO2TC_PULSE_MEAN] = a->n_pu ? (float)(a->pu_sum / a->n_pu) : NAN;
}

// ===================== Xử lý tín hiệu mỗi giây (dựa trên code Arduino của bạn) =====================
#define BUF_SIZE 100                       // ~4 giây ở 25 Hz

static uint32_t ir_buf[BUF_SIZE];
static uint32_t red_buf[BUF_SIZE];
static int      buf_idx = 0;

static float dcIR = 0, smoothIR = 0, prevSmoothIR = 0, prev2SmoothIR = 0;
static int   bpm_avg = 0;
static int   bpm_hist[4] = {0};
static int   bpm_idx = 0;
static bool  is_peak = false;
static int64_t last_beat_us = 0;

static float last_spo2 = 98.0f;            // SpO2 mới nhất tính được
static float last_hr   = 0.0f;

// Trả về 1 nếu giây hợp lệ, 0 nếu không
static int process_samples(const uint32_t *red, const uint32_t *ir, int n)
{
    int valid_samples = 0;

    for (int i = 0; i < n; i++) {
        uint32_t irv  = ir[i];
        uint32_t redv = red[i];

        // Ngưỡng có tiếp xúc (giống code của bạn)
        if (irv <= 40000) {
            // Mất tiếp xúc → reset trạng thái
            dcIR = 0;
            smoothIR = prevSmoothIR = prev2SmoothIR = 0;
            bpm_avg = 0;
            last_hr = 0;
            last_spo2 = 0;
            is_peak = false;
            buf_idx = 0;
            memset(bpm_hist, 0, sizeof(bpm_hist));
            gpio_set_level(GLED_PIN, 0);
            gpio_set_level(RLED_PIN, 1);   // đỏ = không tay
            continue;
        }

        valid_samples++;
        gpio_set_level(GLED_PIN, 1);
        gpio_set_level(RLED_PIN, 0);

        // --- Lưu buffer tính SpO2 ---
        ir_buf[buf_idx]  = irv;
        red_buf[buf_idx] = redv;
        buf_idx = (buf_idx + 1) % BUF_SIZE;

        // Tính SpO2 mỗi khi buffer đầy (giống code Arduino)
        if (buf_idx == 0) {
            float sum_ac_ir = 0, sum_ac_red = 0;
            float avg_dc_ir = 0, avg_dc_red = 0;
            for (int k = 0; k < BUF_SIZE; k++) {
                avg_dc_ir  += ir_buf[k];
                avg_dc_red += red_buf[k];
            }
            avg_dc_ir  /= BUF_SIZE;
            avg_dc_red /= BUF_SIZE;

            for (int k = 0; k < BUF_SIZE; k++) {
                sum_ac_ir  += fabsf((float)ir_buf[k]  - avg_dc_ir);
                sum_ac_red += fabsf((float)red_buf[k] - avg_dc_red);
            }

            if (avg_dc_ir > 0 && avg_dc_red > 0) {
                float r = (sum_ac_red / avg_dc_red) / (sum_ac_ir / avg_dc_ir);
                float spo2 = 104.0f - 17.0f * r;     // công thức tạm của bạn
                if (spo2 > 100.0f) spo2 = 100.0f;
                if (spo2 < 70.0f)  spo2 = 70.0f;    // nới rộng một chút so với 85
                last_spo2 = spo2;
            }
        }

        // --- Tách DC + làm mượt AC để bắt BPM ---
        if (dcIR == 0) dcIR = (float)irv;
        dcIR = 0.92f * dcIR + 0.08f * (float)irv;
        float ac = ((float)irv - dcIR) * -1.0f;

        prev2SmoothIR = prevSmoothIR;
        prevSmoothIR  = smoothIR;
        smoothIR = 0.75f * smoothIR + 0.25f * ac;

        // Bắt đỉnh
        if (prevSmoothIR > 4.0f &&
            prevSmoothIR > prev2SmoothIR &&
            prevSmoothIR > smoothIR &&
            !is_peak)
        {
            is_peak = true;
            int64_t now = esp_timer_get_time();
            int64_t delta = now - last_beat_us;
            if (delta > 330000 && delta < 1500000) {   // 40–180 bpm
                float bpm = 60000000.0f / (float)delta;
                bpm_hist[bpm_idx] = (int)bpm;
                bpm_idx = (bpm_idx + 1) % 4;
                int sum = 0, cnt = 0;
                for (int k = 0; k < 4; k++) {
                    if (bpm_hist[k] > 0) { sum += bpm_hist[k]; cnt++; }
                }
                if (cnt > 0) {
                    bpm_avg = sum / cnt;
                    last_hr = (float)bpm_avg;
                }
            }
            last_beat_us = now;
        }
        if (smoothIR < -1.0f) is_peak = false;
    }

    // Giây hợp lệ nếu có ít nhất vài mẫu tốt + SpO2 hợp lý
    int second_valid = (valid_samples >= 5) && (last_spo2 >= 70.0f) && (last_spo2 <= 100.0f);
    return second_valid;
}

// ===================== Task đọc cảm biến (chạy liên tục) =====================
static void sensor_task(void *arg)
{
    uint32_t red[32], ir[32];
    int64_t last_sec_us = esp_timer_get_time();

    while (1) {
        int n = max30102_read(red, ir, 32);
        if (n > 0) {
            process_samples(red, ir, n);
        }

        // Mỗi 1 giây → thêm vào bộ gộp phút
        int64_t now = esp_timer_get_time();
        if (now - last_sec_us >= 1000000) {
            last_sec_us = now;

            int valid = (last_spo2 >= 70.0f && last_spo2 <= 100.0f && last_hr > 0);
            // Có thể siết SQI chặt hơn ở đây (AC/DC, độ ổn định đỉnh…)

            if (xSemaphoreTake(s_acc_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                acc_add_second(&s_acc, valid, last_spo2, last_hr);
                xSemaphoreGive(s_acc_mutex);
            }

            // Log tạm để debug
            ESP_LOGI(TAG, "sec | SpO2=%.1f  HR=%.0f  valid=%d  n_sp=%d",
                     last_spo2, last_hr, valid, s_acc.n_sp);
        }

        vTaskDelay(pdMS_TO_TICKS(40));   // ~25 Hz
    }
}

// ===================== Publish (tạm thời chỉ log) =====================
static void publish_minute(const float m[SPO2TC_N_CH], const spo2tc_result_t *r)
{
    ESP_LOGI(TAG, "MINUTE | mean=%.1f min=%.1f valid=%.2f pulse=%.0f | level=%d p=%.3f",
             m[SPO2TC_SPO2_MEAN], m[SPO2TC_SPO2_MIN], m[SPO2TC_SPO2_VALID],
             m[SPO2TC_PULSE_MEAN], r->level, r->prob);
}

static void publish_notification(const char *tier, const spo2tc_result_t *r)
{
    ESP_LOGW(TAG, ">>> THÔNG BÁO %s | p=%.3f q=%d", tier, r->prob, r->out_q);
    // TODO: rung motor / gửi BLE / MQTT
}

// ===================== app_main =====================
static const char *LEVEL[] = {"UNKNOWN", "NORMAL", "WATCH", "ALERT", "CRITICAL"};

extern "C" void app_main(void)
{
    // LED
    gpio_set_direction(RLED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(GLED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(INT_PIN,  GPIO_MODE_INPUT);
    gpio_set_level(RLED_PIN, 1);
    gpio_set_level(GLED_PIN, 0);

    // Model + self-test
    if (model_runner_init() != 0 || self_test() != 0) {
        ESP_LOGE(TAG, "model không sẵn sàng – chỉ chạy luật CRITICAL");
    }

    // MAX30102
    if (max30102_init() != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 init FAIL – nháy đỏ");
        while (1) {
            gpio_set_level(RLED_PIN, !gpio_get_level(RLED_PIN));
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    ESP_LOGI(TAG, "MAX30102 OK");

    // Engine
    static spo2tc_engine_t engine;
    spo2tc_config_t cfg;
    spo2tc_config_default(&cfg);
    spo2tc_config_set_T(&cfg, 88);          // mặc định, server sẽ ghi đè
    spo2tc_engine_init(&engine, &cfg);

    // --- BƠM GIẢ LẬP 57 PHÚT TÍN HIỆU ỔN ĐỊNH ---
   // {
   //     float dummy_m[SPO2TC_N_CH];
   //     dummy_m[SPO2TC_SPO2_MEAN]  = 92.0f;
   //     dummy_m[SPO2TC_SPO2_MIN]   = 90.0f;
   //     dummy_m[SPO2TC_SPO2_VALID] = 1.0f;
   //     dummy_m[SPO2TC_PULSE_MEAN] = 75.0f;

   //     for (int i = 0; i < 57; i++) {
   //         spo2tc_engine_push_minute(&engine, dummy_m, NULL, NULL);
   //     }
   //     ESP_LOGI(TAG, "Đã bơm giả lập xong 57 phút tín hiệu ban đầu.");
   // }
    // -------------------------------------------

    // Mutex + reset bộ gộp
    s_acc_mutex = xSemaphoreCreateMutex();
    acc_reset(&s_acc);

    // Task đọc cảm biến
    xTaskCreate(sensor_task, "sensor", 4096, NULL, 5, NULL);

    // Vòng lặp mỗi phút
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(60 * 1000));

        float m[SPO2TC_N_CH];
        if (xSemaphoreTake(s_acc_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            acc_finish(&s_acc, m);
            acc_reset(&s_acc);
            xSemaphoreGive(s_acc_mutex);
        } else {
            // timeout → coi như phút thiếu
            m[SPO2TC_SPO2_MEAN]  = NAN;
            m[SPO2TC_SPO2_MIN]   = NAN;
            m[SPO2TC_SPO2_VALID] = 0.0f;
            m[SPO2TC_PULSE_MEAN] = NAN;
        }

        spo2tc_result_t r = spo2tc_engine_push_minute(&engine, m, model_runner_invoke, NULL);

        publish_minute(m, &r);

        if (r.notify_critical)
            publish_notification(r.critical_reason == 1 ? "CRITICAL_SPO2" : "CRITICAL_HR", &r);
        if (r.notify_alert)
            publish_notification(r.alert_by_rule ? "ALERT_RULE" : "ALERT_ML", &r);
        if (r.notify_watch)
            publish_notification("WATCH", &r);

        if (r.ran_model) {
            ESP_LOGI(TAG, "phút %ld | %s | p=%.3f | baseline %.1f (%.1f h)",
                     (long)engine.t, LEVEL[r.level], r.prob, r.baseline, r.base_hours);
        }
    }
}
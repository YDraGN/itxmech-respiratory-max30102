// app_main.cc - vòng đeo RespiCare: ESP32-C3 SuperMini + MAX30102 + model hybrid_tc
// - Self-test model 24/24 lúc boot
// - Task cảm biến: đọc FIFO ~25 Hz, mỗi giây cộng vào bộ gộp phút và cửa sổ LIVE;
//   mỗi RESPI_LIVE_PERIOD_S giây (15) gửi một LIVE là số hiển thị chung cho app, người nhà, web
// - Vòng lặp phút: engine luật + model → ghi flash → RECORDS / ASSESSMENT qua BLE
// - Mỗi RESPI_DIAG_PERIOD_MIN phút (5): DIAG (CPU, thời gian model / phút / flash) qua BLE hoặc WiFi
// - Kết nối app và dự phòng: link/link_manager.c, kênh BLE: ble/ble_gatt.c

#include <math.h>
#include <stdio.h>
#include "ble_gatt.h"
#include "device_clock.h"
#include "device_state.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "golden_vectors.h"
#include "device_cred.h"
#include "diag.h"
#include "led_status.h"
#include "link_manager.h"
#include "max30102.h"
#include "model_runner.h"
#include "ppg.h"
#include "record_store.h"
#include "sdkconfig.h"
#include "spo2_tc.h"
#include "uplink.h"
#include "vitals.h"
#include "wifi_store.h"
#include "wristband_config.h"

static const char *TAG = "spo2tc";
static const char *LEVEL[] = {"UNKNOWN", "NORMAL", "WATCH", "ALERT", "CRITICAL"};

#define ASSESSMENT_EVERY_MIN 5

static bool s_model_ready = false;

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

// ===================== Task đọc cảm biến (chạy liên tục) =====================
static void sensor_task(void *arg)
{
    uint32_t red[32], ir[32];
    int64_t next_sec_us = esp_timer_get_time() + 1000000;
    int sec_in_window = 0;
    bool last_worn = false;

    while (1) {
        int n = max30102_read(red, ir, 32);
        if (n > 0) {
            ppg_process(red, ir, n);
            led_status_set_finger(ppg_contact_now());   // không đeo: tắt LED (led_pattern.h)
        }

        // Mỗi 1 giây → thêm vào bộ gộp phút và cửa sổ LIVE
        int64_t now = esp_timer_get_time();
        if (now >= next_sec_us) {
            // Giữ nhịp theo mốc cố định (không lấy "now"), nếu không mỗi giây trễ thêm tới 40 ms
            // và một phút chỉ đếm được 59 giây (log cũ: valid tối đa 0,98).
            next_sec_us += 1000000;
            if (now - next_sec_us > 2000000) next_sec_us = now + 1000000;   // bị chặn lâu: bắt nhịp lại

            float spo2 = ppg_last_spo2(), hr = ppg_last_hr();
            // Có thể siết SQI chặt hơn ở đây (AC/DC, độ ổn định đỉnh…)
            int total = 0, contact = 0;
            ppg_take_counts(&total, &contact);
            uint8_t sqi = total ? (uint8_t)(100 * contact / total) : 0;
            bool worn = total > 0 && contact * 2 > total;
            // Không đeo thì ppg_last_* chỉ là số cũ / nhiễu: không tính là số đo (không vào phút, LIVE để trống).
            bool valid = worn && spo2 >= 70.0f && spo2 <= 100.0f && hr > 0;

            vitals_add_second(valid, spo2, hr, worn, sqi);
            diag_tick_second();
            if (worn != last_worn) {
                last_worn = worn;
                ble_gatt_notify_status();               // đặc tả: notify STATUS khi đổi trạng thái đeo
            }

#if CONFIG_RESPI_LOG_EVERY_SECOND
            ESP_LOGI(TAG, "sec | SpO2=%.1f  HR=%.0f  valid=%d  sqi=%u | %s",
                     spo2, hr, valid, sqi, link_manager_state_name(link_manager_state()));
#endif

            // Mỗi RESPI_LIVE_PERIOD_S giây: một số hiển thị (trung bình cửa sổ), thay cho LIVE từng giây.
            if (++sec_in_window >= CONFIG_RESPI_LIVE_PERIOD_S) {
                sec_in_window = 0;
                ble_live_t live;
                vitals_finish_window(&live);
                if (device_clock_is_synced()) ble_gatt_send_live(&live);   // chưa TIME_SYNC thì ts vô nghĩa
                ESP_LOGI(TAG, "live | SpO2=%.1f (min %.1f)  HR=%.0f  valid=%u%%  sqi=%u | %s", live.spo2,
                         live.spo2_min, live.pulse, live.valid_pct, live.sqi_pct,
                         link_manager_state_name(link_manager_state()));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(40));   // ~25 Hz
    }
}

// ===================== Config mới: chỉ sửa engine trong task phút (CONFIG_VA_API.md §4.2) =====================
static void apply_pending_config(spo2tc_engine_t *engine, wb_config_t *active)
{
    wb_config_t nc;
    if (!wb_config_take_pending(&nc)) return;
    int keep_T = engine->cfg.T;
    bool t_changed = nc.eng.T != keep_T;
    engine->cfg = nc.eng;                    // tầng, critical, nhịp tim
    engine->cfg.T = keep_T;
    if (t_changed) spo2tc_engine_set_T(engine, nc.eng.T);   // điền thr_q, xóa lịch sử sự kiện
    *active = nc;
    ESP_LOGI(TAG, "áp dụng config v%u vào engine (T=%d%s)", nc.version, nc.eng.T, t_changed ? ", T đổi" : "");
}

// ===================== Ghi và gửi kết quả một phút =====================
static void publish_minute(const vitals_minute_t *vm, const spo2tc_result_t *r,
                           const spo2tc_engine_t *engine, const wb_config_t *active, bool level_changed)
{
    ble_record_t rec;
    rec.seq = 0;
    rec.ts = vm->ts_start;
    rec.spo2_mean = vm->m[SPO2TC_SPO2_MEAN];
    rec.spo2_min = vm->m[SPO2TC_SPO2_MIN];
    rec.spo2_valid_pct = (uint8_t)lroundf(vm->m[SPO2TC_SPO2_VALID] * 100.0f);
    rec.pulse_mean = vm->m[SPO2TC_PULSE_MEAN];
    rec.sqi_pct = vm->sqi_pct;
    rec.worn = vm->worn;
    int64_t t_flash = esp_timer_get_time();
    record_store_add_telemetry(&rec);        // gán seq, ghi flash trước rồi mới gửi
    diag_note_flash_us(esp_timer_get_time() - t_flash);
    ble_gatt_send_record(&rec);

    vitals_history_push(vm->m);

    bool notify = r->notify_watch || r->notify_alert || r->notify_critical;
    if (engine->t % ASSESSMENT_EVERY_MIN == 0 || level_changed || notify) {
        float slope = vitals_spo2_slope_30m();
        ble_assessment_t a;
        a.seq = rec.seq;
        a.ts = vm->ts_start;
        a.state = (uint8_t)ble_proto_state_from_level(r->level);
        a.trigger = (uint8_t)ble_proto_trigger_from_result(r);
        // 60 phút đầu model chưa đủ cửa sổ nên mức là UNKNOWN dù tín hiệu tốt: đó là "đang khởi động",
        // không phải mất tín hiệu. Chỉ giữ signal_lost khi phút này thật sự ít giây hợp lệ.
        if (a.trigger == BLE_TRIG_SIGNAL_LOST && engine->t < SPO2TC_IN_MIN && vm->m[SPO2TC_SPO2_VALID] >= 0.5f)
            a.trigger = BLE_TRIG_NONE;
        a.out_q = r->ran_model ? r->out_q : (int8_t)-128;
        a.T = (uint8_t)engine->cfg.T;
        a.spo2_now = vm->m[SPO2TC_SPO2_MEAN];
        a.spo2_min_10m = vitals_spo2_min_10m();
        a.spo2_slope_30m = slope;
        a.pulse_now = vm->m[SPO2TC_PULSE_MEAN];
        a.local_notified = false;            // chưa có motor rung
        a.reasons = vitals_reasons(vm, &engine->cfg, slope);
        a.config_version = active->version;
        t_flash = esp_timer_get_time();
        record_store_add_assessment(&a);
        diag_note_flash_us(esp_timer_get_time() - t_flash);
        ble_gatt_send_assessment(&a);
    }
    if (r->notify_alert || r->notify_critical) link_manager_on_urgent();
    led_status_set_level((uint8_t)ble_proto_state_from_level(r->level));   // đỏ chớp khi ALERT / CRITICAL

    ESP_LOGI(TAG, "MINUTE seq %lu | mean=%.1f min=%.1f valid=%.2f pulse=%.0f | %s p=%.3f | %s",
             (unsigned long)rec.seq, rec.spo2_mean, rec.spo2_min, vm->m[SPO2TC_SPO2_VALID], rec.pulse_mean,
             LEVEL[r->level], r->prob, link_manager_state_name(link_manager_state()));
    if (r->notify_critical)
        ESP_LOGW(TAG, ">>> THÔNG BÁO %s", r->critical_reason == 1 ? "CRITICAL_SPO2" : "CRITICAL_HR");
    if (r->notify_alert) ESP_LOGW(TAG, ">>> THÔNG BÁO %s", r->alert_by_rule ? "ALERT_RULE" : "ALERT_ML");
    if (r->notify_watch) ESP_LOGW(TAG, ">>> THÔNG BÁO WATCH");
}

// ===================== app_main =====================
extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(device_state_init());
    diag_init();
    led_status_init();
    gpio_reset_pin((gpio_num_t)CONFIG_RESPI_PIN_MAX_INT);
    gpio_set_direction((gpio_num_t)CONFIG_RESPI_PIN_MAX_INT, GPIO_MODE_INPUT);

    // Model + self-test
    s_model_ready = (model_runner_init() == 0 && self_test() == 0);
    if (!s_model_ready) ESP_LOGE(TAG, "model không sẵn sàng – chỉ chạy luật");

    // Lưu trữ, config, kết nối
    if (record_store_init() != ESP_OK) ESP_LOGE(TAG, "không mở được vùng lưu bản ghi – dữ liệu chỉ gửi trực tiếp");
    wb_config_init();
    wifi_store_init();
    device_cred_init();       // khóa thiết bị cho chế độ WiFi (app ghi qua BLE)
    vitals_init();
    uplink_init();
    link_manager_init();      // LED nháy nhanh: đang chờ app
    ble_gatt_init();

    // MAX30102
    bool sensor_ok = (max30102_init() == ESP_OK);
    if (sensor_ok) {
        ESP_LOGI(TAG, "MAX30102 OK");
    } else {
        ESP_LOGE(TAG, "MAX30102 init FAIL – nháy đỏ, BLE vẫn chạy để app thấy thiết bị");
        led_status_error();
    }

    // Engine
    static spo2tc_engine_t engine;
    wb_config_t active;
    wb_config_get(&active);
    spo2tc_engine_init(&engine, &active.eng);

    if (sensor_ok) xTaskCreate(sensor_task, "sensor", 4096, NULL, 5, NULL);

    // Vòng lặp mỗi phút
    spo2tc_level_t prev_level = SPO2TC_UNKNOWN;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(CONFIG_RESPI_MINUTE_PERIOD_S * 1000));
        int64_t t_minute = esp_timer_get_time();

        apply_pending_config(&engine, &active);

        vitals_minute_t vm;
        vitals_finish_minute(&vm);

        spo2tc_model_fn fn = (active.ml_enabled && active.model_ok && s_model_ready) ? model_runner_invoke : NULL;
        int64_t t_engine = esp_timer_get_time();
        spo2tc_result_t r = spo2tc_engine_push_minute(&engine, vm.m, fn, NULL);
        int64_t engine_us = esp_timer_get_time() - t_engine;
        // Phút có chạy model: tính cả baseline + đặc trưng + lượng tử + TFLite (giống self-test lúc boot).
        if (r.ran_model) diag_note_model_us(engine_us);

        publish_minute(&vm, &r, &engine, &active, r.level != prev_level);
        prev_level = r.level;
        diag_note_minute_us(esp_timer_get_time() - t_minute);

        if (r.ran_model) {
            ESP_LOGI(TAG, "phút %ld | %s | p=%.3f | baseline %.1f (%.1f h) | engine %.1f ms",
                     (long)engine.t, LEVEL[r.level], r.prob, r.baseline, r.base_hours, engine_us / 1000.0);
        }

        // Cửa sổ DIAG khớp nhịp model (mặc định 5 phút) để mỗi cửa sổ có đúng một lần chạy model.
        if (engine.t % CONFIG_RESPI_DIAG_PERIOD_MIN == 0) {
            diag_finish_window(NULL);
            ble_gatt_send_diag();   // không có app: ở lại vòng đệm, uplink gửi ở lượt WiFi kế tiếp
        }
    }
}

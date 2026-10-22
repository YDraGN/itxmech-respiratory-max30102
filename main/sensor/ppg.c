#include "ppg.h"

#include <math.h>
#include <string.h>
#include "esp_timer.h"

/* ===================== Xử lý tín hiệu (dựa trên code Arduino của nhóm phần cứng) ===================== */
#define BUF_SIZE 100                       // ~4 giây ở 25 Hz
#define CONTACT_IR_MIN 40000u              // ngưỡng có tiếp xúc

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

static int count_total = 0, count_contact = 0;
static bool contact_now = false;           // mẫu cuối cùng có tiếp xúc (bản cũ dùng để bật LED)

void ppg_process(const uint32_t *red, const uint32_t *ir, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t irv  = ir[i];
        uint32_t redv = red[i];
        count_total++;
        contact_now = irv > CONTACT_IR_MIN;

        if (!contact_now) {
            // Mất tiếp xúc → reset trạng thái
            dcIR = 0;
            smoothIR = prevSmoothIR = prev2SmoothIR = 0;
            bpm_avg = 0;
            last_hr = 0;
            last_spo2 = 0;
            is_peak = false;
            buf_idx = 0;
            memset(bpm_hist, 0, sizeof(bpm_hist));
            continue;
        }
        count_contact++;

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
                float spo2 = 104.0f - 17.0f * r;     // công thức tạm của nhóm phần cứng
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
}

float ppg_last_spo2(void) { return last_spo2; }
float ppg_last_hr(void) { return last_hr; }
bool ppg_contact_now(void) { return contact_now; }

void ppg_take_counts(int *total, int *with_contact)
{
    *total = count_total;
    *with_contact = count_contact;
    count_total = count_contact = 0;
}

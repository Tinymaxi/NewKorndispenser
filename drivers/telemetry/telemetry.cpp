#include "telemetry.hpp"
#include "pico/stdlib.h"
#include "hardware/sync.h"

static TelemetrySample s_buf[TELEM_CAPACITY];
static volatile uint32_t s_count  = 0;
static volatile uint32_t s_run_id = 0;
static volatile bool     s_active = false;

// Run metadata (written only between runs / at run boundaries)
static uint8_t  s_scale    = 0;
static uint16_t s_target_g = 0;
static float    s_kp = 0, s_ki = 0, s_kd = 0;
static float    s_final_g  = 0;

void telem_begin_run(uint8_t scale, uint16_t target_g, float kp, float ki, float kd) {
    // Caller holds the lwIP lock (cyw43_arch_lwip_begin) so an in-flight CSV
    // reader cannot observe the reset mid-row.
    s_count  = 0;
    s_scale  = scale;
    s_target_g = target_g;
    s_kp = kp; s_ki = ki; s_kd = kd;
    s_final_g = 0;
    s_active  = true;
    s_run_id  = s_run_id + 1;
}

void telem_append(const TelemetrySample& s) {
    uint32_t idx = s_count;
    if (idx >= TELEM_CAPACITY) return;   // buffer full - stop recording
    s_buf[idx] = s;
    __dmb();          // row fully written before publishing the new count
    s_count = idx + 1;
}

void telem_end_run(float final_g) {
    s_final_g = final_g;
    s_active  = false;
}

TelemetryMeta telem_meta() {
    TelemetryMeta m;
    m.run_id  = s_run_id;
    m.count   = s_count;
    m.active  = s_active;
    m.scale   = s_scale;
    m.target_g = s_target_g;
    m.kp = s_kp; m.ki = s_ki; m.kd = s_kd;
    m.final_g = s_final_g;
    return m;
}

const TelemetrySample* telem_sample(uint32_t idx) {
    if (idx >= s_count) return nullptr;
    return &s_buf[idx];
}

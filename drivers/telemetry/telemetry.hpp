#pragma once
#include <cstdint>

// PID tuning telemetry: captures one sample per actual PID computation (20 Hz)
// during a dispense run into a fixed linear buffer, for the /api/log.csv endpoint.
//
// Concurrency contract (single core, no locks needed for reads):
//   - Writer: main loop only (telem_begin_run / telem_append / telem_end_run).
//   - Reader: lwIP callbacks in background-IRQ context (telem_meta / telem_sample).
//   - telem_append fills the row completely BEFORE incrementing the published count,
//     so a reader never sees a half-written sample.
//   - telem_begin_run resets the buffer; the CALLER must wrap it in
//     cyw43_arch_lwip_begin()/end() so the reset cannot interleave with an
//     in-flight CSV send. Readers snapshot run_id and drop the connection if it
//     changes mid-stream.
//
// Linear capture, not a ring: recording simply stops when full (first ~100 s of a
// run at 20 Hz). Within one run a written sample is immutable — lock-free reads.

struct TelemetrySample {
    uint32_t t_ms;       // ms since dispense start
    float setpoint;      // target grams
    float dispensed;     // PID input (grams dispensed so far)
    float weight;        // tare-relative scale reading (grams)
    float gross;         // absolute bag weight vs calibrated zero (grams)
    float servo;         // PID output = servo angle (degrees)
    float p, i, d;       // PID term contributions (GetLastP/I/D)
    float vib;           // vibrator intensity 0..1
};
static_assert(sizeof(TelemetrySample) == 40, "unexpected padding");

inline constexpr uint32_t TELEM_CAPACITY = 2000;   // 100 s @ 20 Hz, ~70 KB static

struct TelemetryMeta {
    uint32_t run_id;     // increments each begin_run; 0 = no run yet
    uint32_t count;      // valid samples
    bool     active;     // run in progress
    uint8_t  scale;      // 0..2
    uint16_t target_g;
    float    kp, ki, kd;
    float    final_g;    // set by end_run (0 while active)
    char     name[16];   // scale contents at run start ("Wheat", ...)
};

void telem_begin_run(uint8_t scale, uint16_t target_g, float kp, float ki, float kd,
                     const char* name);
void telem_append(const TelemetrySample& s);        // drops silently when full
void telem_end_run(float final_g);
TelemetryMeta telem_meta();                          // safe from IRQ context
const TelemetrySample* telem_sample(uint32_t idx);   // nullptr if idx >= count

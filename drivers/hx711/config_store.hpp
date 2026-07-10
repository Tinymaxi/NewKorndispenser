#pragma once
#include <cstdint>

// Stores calibration for up to 3 HX711 instances
struct ScaleEntry {
    int32_t offset_counts = 0;
    float   count_per_g   = 0.0f;  // counts per gram
};

struct ScaleConfig {
    uint32_t magic = 0x48583133;   // "HX13" (HX711, 3 channels)
    ScaleEntry entries[3];         // 3 sets of calibration
    uint32_t crc32 = 0;
};

bool load_scale_config(ScaleConfig& cfg);
bool save_scale_config(const ScaleConfig& cfg);

// PID tuning gains, persisted in the second-to-last flash sector (the scale
// calibration above owns the last sector and is never touched by these).
struct PidConfig {
    uint32_t magic = 0x50494431;   // "PID1"
    float kp = 0.0f;
    float ki = 0.0f;
    float kd = 0.0f;
    uint32_t crc32 = 0;
};

bool load_pid_config(PidConfig& cfg);
bool save_pid_config(const PidConfig& cfg);

template <class HX711>
inline void apply_scale_config(HX711& scale, const ScaleEntry& e) {
    scale.set_offset(e.offset_counts);
    scale.set_cal_offset(e.offset_counts);   // calibrated zero for gross weight (survives tare)
    if (e.count_per_g != 0.0f) {
        // hx711::set_scale expects counts/gram internally
        scale.set_scale(e.count_per_g);
    }
}
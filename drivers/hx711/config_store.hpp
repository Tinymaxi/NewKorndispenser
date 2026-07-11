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

// Per-scale content names ("Wheat", "Spelt", ...), persisted in the
// third-to-last flash sector. Changed whenever a bag is swapped.
inline constexpr int SCALE_NAME_LEN = 16;   // 15 chars + NUL

struct NameConfig {
    uint32_t magic = 0x4E414D31;   // "NAM1"
    char names[3][SCALE_NAME_LEN] = {{0}, {0}, {0}};
    uint32_t crc32 = 0;
};

bool load_name_config(NameConfig& cfg);
bool save_name_config(const NameConfig& cfg);

// Per-servo flow-start ("zero") angle in degrees, persisted in the
// fourth-to-last flash sector. open_deg < 0 = servo not calibrated.
struct ServoConfig {
    uint32_t magic = 0x53525631;   // "SRV1"
    float open_deg[3] = {-1.0f, -1.0f, -1.0f};
    uint32_t crc32 = 0;
};

bool load_servo_config(ServoConfig& cfg);
bool save_servo_config(const ServoConfig& cfg);

// Network boot mode, persisted in the fifth-to-last flash sector. Chosen on
// the LCD at boot (encoder); the last choice is the default for next boot.
struct NetConfig {
    uint32_t magic = 0x4E455431;   // "NET1"
    uint8_t  mode = 0;             // 0 = home WiFi (STA), 1 = hotspot (AP)
    uint8_t  pad[3] = {0};
    uint32_t crc32 = 0;
};

bool load_net_config(NetConfig& cfg);
bool save_net_config(const NetConfig& cfg);

template <class HX711>
inline void apply_scale_config(HX711& scale, const ScaleEntry& e) {
    scale.set_offset(e.offset_counts);
    scale.set_cal_offset(e.offset_counts);   // calibrated zero for gross weight (survives tare)
    if (e.count_per_g != 0.0f) {
        // hx711::set_scale expects counts/gram internally
        scale.set_scale(e.count_per_g);
    }
}
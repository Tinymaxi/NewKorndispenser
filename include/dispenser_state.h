#ifndef _DISPENSER_STATE_H
#define _DISPENSER_STATE_H

#include <cstdint>

enum class WebCommand : uint8_t {
    None = 0,
    Tare,
    StartDispense,
    StopDispense,
    SetTarget,
    SelectScale,
    TestServo,
    TestVibrator,
    TestStop,
    Calibrate,
    SetPID,
    SetName,
    SetServoZero,
};

// One queued web command with its payload
struct WebCmd {
    WebCommand cmd = WebCommand::None;
    int   i0 = 0;                  // target grams / scale index / cal weight
    float f0 = 0, f1 = 0, f2 = 0;  // servo angle / vib intensity / kp,ki,kd
    char  s0[16] = {0};            // scale content name (SetName)
};

inline constexpr uint8_t WEBCMD_QUEUE_LEN = 8;

struct DispenserState {
    // --- Written by main loop, read by web server ---
    float weights[3]       = {0, 0, 0};   // Tare-relative weight per scale (grams)
    float gross[3]         = {0, 0, 0};   // Absolute (bag) weight per scale (grams),
                                          // vs the calibrated zero - unaffected by tare
    char  names[3][16]     = {{0},{0},{0}};  // Scale contents ("Wheat", "Spelt", ...)
    int   selected_scale   = 0;            // 0-2
    int   target_grams     = 100;
    bool  dispensing       = false;
    bool  dispense_done    = false;
    float dispensed_grams  = 0;            // Amount dispensed so far
    bool  scale_calibrated[3] = {false, false, false};
    float pid_kp = 0;
    float pid_ki = 0;
    float pid_kd = 0;
    float servo_angle    = 0;              // Current servo angle (degrees)
    float vib_intensity  = 0;              // Current vibrator intensity (0-1)
    bool  ap_mode        = false;          // True when serving own AP instead of joining WiFi
    float servo_zero[3]  = {-1, -1, -1};   // Calibrated flow-start angle per servo
                                           // (degrees); < 0 = not calibrated

    // --- Command ring queue: web server (lwIP context) pushes at head, main loop
    // drains from tail under the lwIP lock. A queue (not a single slot) so commands
    // issued while the main loop is busy (e.g. a ~1.5 s blocking tare) are not lost.
    WebCmd cmd_queue[WEBCMD_QUEUE_LEN];
    volatile uint8_t cmd_head = 0;   // next write slot (web server)
    volatile uint8_t cmd_tail = 0;   // next read slot (main loop)
};

// --- Servo working-range helpers -------------------------------------------
// Each dispenser mechanism differs, so the angle where grain starts to flow is
// calibrated per servo (servo_zero[], jogged + saved by the user). Servos
// without a calibration keep the historical fixed values.

inline constexpr float SERVO_CLOSE_BACKOFF_DEG = 15.0f;  // close this far below zero
inline constexpr float SERVO_DEFAULT_MIN_OPEN  = 85.0f;  // uncalibrated PID floor
inline constexpr float SERVO_FULL_OPEN         = 170.0f; // PID ceiling

inline bool servo_zero_set(const DispenserState& s, int i) {
    return s.servo_zero[i] >= 0.0f && s.servo_zero[i] <= 180.0f;
}

// PID lower output limit. Clamped below SERVO_FULL_OPEN because
// PID::SetOutputLimits(min, max) silently ignores min >= max.
inline float servo_min_open(const DispenserState& s, int i) {
    if (!servo_zero_set(s, i)) return SERVO_DEFAULT_MIN_OPEN;
    float z = s.servo_zero[i];
    return z > SERVO_FULL_OPEN - 5.0f ? SERVO_FULL_OPEN - 5.0f : z;
}

// Physical closed position: just below the flow-start point instead of a full
// sweep to 0 - faster, gentler closes. Uncalibrated: 0 (historical behavior).
inline float servo_close(const DispenserState& s, int i) {
    if (!servo_zero_set(s, i)) return 0.0f;
    float c = s.servo_zero[i] - SERVO_CLOSE_BACKOFF_DEG;
    return c < 0.0f ? 0.0f : c;
}

#endif // _DISPENSER_STATE_H

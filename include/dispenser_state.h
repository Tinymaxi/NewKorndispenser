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
};

struct DispenserState {
    // --- Written by main loop, read by web server ---
    float weights[3]       = {0, 0, 0};   // Current weight on each scale (grams)
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

    // --- Written by web server, read by main loop ---
    volatile WebCommand pending_command = WebCommand::None;
    int   cmd_target       = 0;            // For SetTarget
    int   cmd_scale        = 0;            // For SelectScale
    float cmd_servo_angle  = 0.0f;         // For TestServo
    float cmd_vib_intensity = 0.0f;        // For TestVibrator
    int   cmd_cal_weight   = 0;            // For Calibrate (known grams)
    float cmd_pid_kp       = 0;            // For SetPID
    float cmd_pid_ki       = 0;
    float cmd_pid_kd       = 0;
};

#endif // _DISPENSER_STATE_H

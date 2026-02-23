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
};

struct DispenserState {
    // --- Written by main loop, read by web server ---
    float weights[3]       = {0, 0, 0};   // Current weight on each scale (grams)
    int   selected_scale   = 0;            // 0-2
    int   target_grams     = 100;
    bool  dispensing       = false;
    bool  dispense_done    = false;
    float dispensed_grams  = 0;            // Amount dispensed so far
    float start_weight     = 0;            // Weight when dispense began
    bool  scale_calibrated[3] = {false, false, false};

    // --- Written by web server, read by main loop ---
    volatile WebCommand pending_command = WebCommand::None;
    int   cmd_target       = 0;            // For SetTarget
    int   cmd_scale        = 0;            // For SelectScale
    float cmd_servo_angle  = 60.0f;        // For TestServo
    float cmd_vib_intensity = 0.0f;        // For TestVibrator
    int   cmd_cal_weight   = 0;            // For Calibrate (known grams)
};

#endif // _DISPENSER_STATE_H

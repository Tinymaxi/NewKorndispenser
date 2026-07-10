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

// One queued web command with its payload
struct WebCmd {
    WebCommand cmd = WebCommand::None;
    int   i0 = 0;                  // target grams / scale index / cal weight
    float f0 = 0, f1 = 0, f2 = 0;  // servo angle / vib intensity / kp,ki,kd
};

inline constexpr uint8_t WEBCMD_QUEUE_LEN = 8;

struct DispenserState {
    // --- Written by main loop, read by web server ---
    float weights[3]       = {0, 0, 0};   // Tare-relative weight per scale (grams)
    float gross[3]         = {0, 0, 0};   // Absolute (bag) weight per scale (grams),
                                          // vs the calibrated zero - unaffected by tare
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

    // --- Command ring queue: web server (lwIP context) pushes at head, main loop
    // drains from tail under the lwIP lock. A queue (not a single slot) so commands
    // issued while the main loop is busy (e.g. a ~1.5 s blocking tare) are not lost.
    WebCmd cmd_queue[WEBCMD_QUEUE_LEN];
    volatile uint8_t cmd_head = 0;   // next write slot (web server)
    volatile uint8_t cmd_tail = 0;   // next read slot (main loop)
};

#endif // _DISPENSER_STATE_H

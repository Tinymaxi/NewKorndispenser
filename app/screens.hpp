#pragma once
#include <cstdint>

// LCD/encoder UI screens, one class per screen (state pattern).
//
// Lifecycle: ScreenManager calls enter() once when a screen becomes current
// (draw the static header, capture encoder/button state so presses don't carry
// over from the previous screen), then update() every loop tick. update()
// returns the ScreenId to show next - itself to stay put.
//
// Per-screen state that used to live in function-local statics inside main()'s
// big switch now lives in member variables, initialized in enter().

class Lcd1602I2C;
class Rotary_Button;
class Buzzer;
class hx711;
class Servo;
class Vibrator;
class SevenSeg;
class PID;
struct ScaleConfig;
struct DispenserState;

enum class ScreenId {
    Menu,
    SelectScale,
    Calibrate1,
    Calibrate2,
    SetTarget,
    SetTargetDigit,
    Weigh,
    Dispense,
    TestMenu,
    TestVibrator,
    TestServo
};

// Everything a screen may touch: hardware, shared state, and the cross-screen
// UI variables (selection, target, navigation intents from the web side).
struct UiContext {
    Lcd1602I2C&     lcd;
    Rotary_Button&  enc;
    Buzzer&         bz;
    hx711**         scales;      // [3]
    Servo**         servos;      // [3]
    Vibrator**      vibrators;   // [3]
    SevenSeg*&      sevenSeg;    // created after startup animation
    ScaleConfig&    sc;
    DispenserState& g_state;

    // PID tuning state shared with the web command dispatcher in main()
    double& Kp;
    double& Ki;
    double& Kd;
    PID*&   dispense_pid;

    // lwIP lock (no-ops when the network stack is down)
    void (*net_lock)();
    void (*net_unlock)();

    // Cross-screen UI state
    int  selected_scale = 0;     // 0..2
    int  target_grams   = 100;
    ScreenId after_select = ScreenId::Calibrate1;  // where SelectScale leads
    ScreenId after_target = ScreenId::Weigh;       // where SetTargetDigit returns
    bool web_start_dispense = false;  // web requested a dispense start
    bool web_stop_dispense  = false;  // web requested a dispense stop
    bool web_active         = false;  // web controls; local input mostly disabled
};

class Screen {
public:
    virtual ~Screen() = default;
    virtual void enter(UiContext& ctx) = 0;
    virtual ScreenId update(UiContext& ctx) = 0;
};

class ScreenManager {
public:
    // Draws the first screen. Call after LCD/encoder are initialized.
    void init(UiContext& ctx, ScreenId start);

    // One loop tick: update the current screen, perform a requested transition.
    void tick(UiContext& ctx);

    // Forced jump with (re-)enter - used by the web command dispatcher and the
    // web-control unlatch. Re-entering the current screen redraws its header
    // and resets its per-visit state.
    void goTo(UiContext& ctx, ScreenId id);

    ScreenId currentId() const { return current_; }

private:
    ScreenId current_ = ScreenId::Menu;
};

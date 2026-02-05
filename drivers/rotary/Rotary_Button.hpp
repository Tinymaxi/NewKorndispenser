#pragma once
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "Ws2812.hpp"
#include "NeopixelRing.hpp"

class Rotary_Button
{
public:
    Rotary_Button();

    int getPosition();
    void setZero();
    bool isPressed();

private:
    // Existing encoder/button definitions
    static constexpr uint BUTTON_PIN = 15;

    // Hardcoded ring settings
    static constexpr uint RING_PIN = 3;
    static constexpr uint RING_COUNT = 12;
    static constexpr uint PIN_AB = 13;
    static constexpr uint SM_INDEX = 0;
    static constexpr int POSITION_DIVISOR = 4;
    static constexpr uint32_t DEBOUNCE_MS = 80;  // Minimum ms between position changes
    static constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;  // Button debounce time
    static constexpr uint32_t ENCODER_LOCKOUT_MS = 100; // Ignore encoder after button press
    static constexpr uint8_t ON_R = 40;
    static constexpr uint8_t ON_G = 15;
    static constexpr uint8_t ON_B = 0;
    static constexpr uint8_t OFF_R = 0, OFF_G = 0, OFF_B = 0;

    // PIO used for encoder
    PIO encoder_pio_ = nullptr;

    // Ring objects (pointers - created after encoder PIO to avoid conflict)
    Ws2812* ws_ = nullptr;
    NeopixelRing* ring_ = nullptr;

    // Track last rendered state
    int last_idx_ = -1;
    bool last_pressed_ = false;

    // Debounce state
    int debounced_pos_ = 0;
    uint32_t last_change_time_ = 0;

    // Button state
    bool last_raw_button_ = false;

    // Position history - use stable position from before any slip
    int stable_pos_ = 0;           // Position that was stable
    uint32_t last_move_time_ = 0;  // When encoder last moved
    static constexpr uint32_t STABLE_MS = 150;  // Position must be stable this long

    // Animation state
    int prev_pos_ = 0;             // Previous position for direction detection
    int turn_direction_ = 1;       // 1 = clockwise, -1 = counter-clockwise
    uint32_t last_render_time_ = 0;
    uint32_t press_start_time_ = 0;
    bool press_anim_active_ = false;
    static constexpr uint32_t PULSE_PERIOD_MS = 2000;  // Breathing cycle (slower)
    static constexpr uint32_t IDLE_AFTER_MS = 200;     // Start pulsing after this idle time
    static constexpr uint32_t PRESS_ANIM_MS = 350;     // Press animation duration (longer)

    static inline uint8_t wrap_index(int v, uint8_t n)
    {
        int m = v % (int)n;
        return (uint8_t)(m < 0 ? m + n : m);
    }
    void updateRing_(int pos, bool pressed);

    // Your existing raw reads
    int readPositionRaw_() const;
    bool readPressedRaw_() const;
};
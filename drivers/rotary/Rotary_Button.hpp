#pragma once
#include "pico/stdlib.h"
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
    static constexpr uint BUTTON_PIN = 15; // adjust if needed

    // Hardcoded ring settings
    static constexpr uint RING_PIN = 3;
    static constexpr uint RING_COUNT = 12;
    static constexpr uint PIN_AB = 13;
    static constexpr uint SM_INDEX = 0;
    static constexpr int POSITION_DIVISOR = 4;
    static constexpr uint8_t ON_R = 40; // warm red component
    static constexpr uint8_t ON_G = 15; // subtle green for orange
    static constexpr uint8_t ON_B = 0;  // no blue
    static constexpr uint8_t OFF_R = 0, OFF_G = 0, OFF_B = 0;

    // Ring objects
    Ws2812 ws_{RING_PIN, RING_COUNT};
    NeopixelRing ring_{ws_, (uint8_t)RING_COUNT};

    // Track last rendered state
    int last_idx_ = -1;
    bool last_pressed_ = false;

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
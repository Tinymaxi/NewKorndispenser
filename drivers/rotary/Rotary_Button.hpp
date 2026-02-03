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
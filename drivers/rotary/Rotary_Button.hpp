#pragma once
#include "pico/stdlib.h"
#include "Ws2812.hpp"

class Rotary_Button {
public:
    Rotary_Button();
    int getPosition();
    void setZero();
    bool isPressed();
    

private:
    // pins / PIO settings
    static constexpr uint PIN_AB     = 13;
    static constexpr uint BUTTON_PIN = 15;
    static constexpr uint SM_INDEX   = 0;
    static constexpr int POSITION_DIVISOR = 4;
};
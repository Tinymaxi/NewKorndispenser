#pragma once
#include "pico/stdlib.h"
#include "Ws2812.hpp"

class Rotary_Button {
public:
    Rotary_Button();
    void poll();                     // call from main loop / timer

    int  getPosition() const { return s_.pos; }
    bool isPressed()  const { return s_.pressed; }
    void setZero();

private:
    // pins / PIO settings
    static constexpr uint PIN_AB     = 13;
    static constexpr uint BUTTON_PIN = 15;
    static constexpr uint SM_INDEX   = 0;

    // cached state
    struct { int pos; bool pressed; } s_{0,false};
    struct { int pos; bool invert;  } prev_{-1,false};

    Ws2812 ring_{3,12};
    uint offset_ = 0;

    void refreshRing();
};
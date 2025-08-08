#pragma once
#include "hardware/pio.h"

class Ws2812 {
public:
    enum class Colour { Off, Red, Green, Blue };   // easily extend later
    enum class Mode   { Single, Inverted };

    Ws2812(uint pin = 3, uint leds = 12);

    void update(uint index, Mode m, Colour c = Colour::Green);

private:
    void putPixel(uint32_t grb);
    static uint32_t toGrb(Colour c);

    static constexpr uint  sm   = 0;
    static constexpr float freq = 800'000;     // Hz

    uint offset_ = 0;
    uint n_leds_ = 0;
};
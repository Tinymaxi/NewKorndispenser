#pragma once
#include <cstdint>
#include "Ws2812.hpp"

class NeopixelRing {
public:
    explicit NeopixelRing(Ws2812& strip, uint8_t count);

    uint8_t count() const { return count_; }

    void clear();
    void fill(uint8_t r, uint8_t g, uint8_t b);
    void setIndex(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);

    // Fill background color, then light one pixel
    void highlight(uint8_t idx, uint8_t r, uint8_t g, uint8_t b,
                   uint8_t bgR = 0, uint8_t bgG = 0, uint8_t bgB = 0);

    // Fill foreground color everywhere except one index
    void invertAt(uint8_t idx, uint8_t onR, uint8_t onG, uint8_t onB,
                  uint8_t offR = 0, uint8_t offG = 0, uint8_t offB = 0);

    void show();

private:
    Ws2812&  strip_;
    uint8_t  count_;
};
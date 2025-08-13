#include "NeopixelRing.hpp"

NeopixelRing::NeopixelRing(Ws2812& strip, uint8_t count)
: strip_(strip), count_(count) {}

void NeopixelRing::clear() {
    strip_.clear();
}

void NeopixelRing::fill(uint8_t r, uint8_t g, uint8_t b) {
    for (uint8_t i = 0; i < count_; ++i) {
        strip_.setPixel(i, r, g, b);
    }
}

void NeopixelRing::setIndex(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    if (idx < count_) {
        strip_.setPixel(idx, r, g, b);
    }
}

void NeopixelRing::highlight(uint8_t idx, uint8_t r, uint8_t g, uint8_t b,
                             uint8_t bgR, uint8_t bgG, uint8_t bgB) {
    fill(bgR, bgG, bgB);
    setIndex(idx, r, g, b);
}

void NeopixelRing::invertAt(uint8_t idx, uint8_t onR, uint8_t onG, uint8_t onB,
                            uint8_t offR, uint8_t offG, uint8_t offB) {
    fill(onR, onG, onB);
    setIndex(idx, offR, offG, offB);
}

void NeopixelRing::show() {
    strip_.show();
}
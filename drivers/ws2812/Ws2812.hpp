#pragma once
#include <cstdint>
#include <vector>
#include "hardware/pio.h"

class Ws2812 {
public:
    Ws2812(uint pin, uint leds);

    // C-style helper (optional, keeps parity with examples)
    static inline void put_pixel(PIO pio, uint sm, uint32_t pixel_grb) {
        pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
    }

    // Instance-bound write of a single GRB word (rarely needed once you have show())
    inline void putPixelGRB(uint32_t pixel_grb) {
        pio_sm_put_blocking(pio_, sm_, pixel_grb << 8u);
    }

    // Buffer-based API
    void setPixel(uint i, uint8_t r, uint8_t g, uint8_t b); // i in [0, leds)
    void clear();
    void show();

    // (optional) getters
    uint length() const { return leds_; }
    uint pin() const { return pin_; }

private:
    // GRB packer (WS2812 expects G in bits 16–23, R in 8–15, B in 0–7)
    static inline uint32_t pack_grb(uint8_t r, uint8_t g, uint8_t b) {
        return (uint32_t(g) << 16) | (uint32_t(r) << 8) | uint32_t(b);
    }

    // Resources claimed/used by this strip instance
    PIO  pio_    = nullptr;
    uint sm_     = 0;
    uint offset_ = 0;
    uint pin_    = 0;
    uint leds_   = 0;

    std::vector<uint32_t> buf_; // 24‑bit GRB per LED (unshifted)
};
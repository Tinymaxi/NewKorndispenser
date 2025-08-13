#pragma once
#include <cstdint>
#include "Ws2812.hpp"

// 7 segments + dot = 8 “segments” per digit
enum : uint8_t { SEG_A=0, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F, SEG_G, SEG_P, SEG_COUNT=8 };

// A simple layout: for digit d (0..digits-1) and segment s (A..P),
// mapping[d*SEG_COUNT + s] gives the WS2812 pixel index on the strip.
struct SevenSegLayout {
    uint8_t digits;            // number of digits (you said 6)
    const uint16_t* mapping;   // size = digits * SEG_COUNT (indexes into the Ws2812 strip)
};

class SevenSeg {
public:
    SevenSeg(Ws2812& strip, const SevenSegLayout& layout)
    : strip_(strip), layout_(layout) {}

    // Global operations
    void clear();
    void show();

    // Set a whole digit by numeric value (0..9) or minus (-1)
    void setDigit(uint8_t digit, int value, uint8_t r, uint8_t g, uint8_t b);

    // Low-level: set segments via bitmask (bit 0=A ... bit 6=G; dot handled separately)
    void setDigitMask(uint8_t digit, uint8_t segMask, uint8_t r, uint8_t g, uint8_t b);

    // Dot (decimal point) control for a digit
    void setDot(uint8_t digit, bool on, uint8_t r, uint8_t g, uint8_t b);

    // High-level: print a signed integer (truncates/pads to available digits)
    // leftAlign=false -> right aligned (typical), true -> left aligned
    void printNumber(int value, uint8_t r, uint8_t g, uint8_t b, bool leftAlign=false);

private:
    Ws2812& strip_;
    SevenSegLayout layout_;

    // Glyphs for digits 0..9 and minus:
    // bits: a=1<<0, b=1<<1, ..., g=1<<6  (dot handled separately)
    static uint8_t glyphFor(int value);

    inline uint16_t pix(uint8_t digit, uint8_t seg) const {
        return layout_.mapping[digit*SEG_COUNT + seg];
    }
};

extern const SevenSegLayout LAYOUT_6DIGITS;
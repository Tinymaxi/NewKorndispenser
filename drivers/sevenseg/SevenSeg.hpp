#pragma once
#include "Ws2812.hpp"


// 7-segment driver for 6-digit display made from WS2812 LEDs.
// Uses its own state-machine on the *same* PIO block as the ring.
class SevenSeg {
public:
    // pin = data-in GPIO,  digits = how many 7-seg digits (max 8 per strip for now)
    SevenSeg(uint pin = 4, uint digits = 6);

    // write one ASCII character ('0'-'9', '-', ' ') to a digit (0 = left-most)
    // dp = light decimal-point (segment P) as well
    void setChar(uint digit, char c, bool dp = false);

    // convenience helpers
    void showNumber(int value);               // prints signed integer, right-aligned
    void clear()            { fill(' '); }    // blank all digits

private:
    void fill(char c);        // internal helper
    void flush();             // push buffered pixel data to strip

    Ws2812 strip_;                    // one more state-machine on PIO1
    uint   n_digits_;
    uint32_t buf_[8 * 8];             // max 8 digits × 8 LEDs (A..G,P)

    static uint32_t segPattern(char c);   // lookup table: char -> 8-bit pattern
};
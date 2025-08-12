#include "SevenSeg.hpp"

using Colour = Ws2812::Colour;     // reuse enum from your strip driver

// ---------------------------------------------------------------------------
//   Char->segment pattern (bit0 = A, bit1 = B … bit6 = G, bit7 = P)
// ---------------------------------------------------------------------------
static uint8_t const lut[128] = {
/* 0x00-0x1F */ 0,
    // …
/* '-' 0x2D */ 0b0100'0000,
/* '.' 0x2E */ 0b1000'0000,
/* '0' 0x30 */ 0b0011'1111,
/* '1' 0x31 */ 0b0000'0110,
/* '2' 0x32 */ 0b0101'1011,
/* '3' 0x33 */ 0b0100'1111,
/* '4' 0x34 */ 0b0110'0110,
/* '5' 0x35 */ 0b0110'1101,
/* '6' 0x36 */ 0b0111'1101,
/* '7' 0x37 */ 0b0000'0111,
/* '8' 0x38 */ 0b0111'1111,
/* '9' 0x39 */ 0b0110'1111,
/* ':'-'@'   */ 0,
/* 'A'-'Z'   */ 0, /* keep simple – add later if needed */
};

uint32_t SevenSeg::segPattern(char c) {
    if (static_cast<unsigned>(c) < sizeof(lut)) return lut[static_cast<uint8_t>(c)];
    return 0;
}

// ---------------------------------------------------------------------------

SevenSeg::SevenSeg(uint pin, uint digits)
    : strip_{pin, digits * 8}, n_digits_{digits}
{
    clear();         // buf_[] = off
    flush();         // push once
}

void SevenSeg::fill(char c) {
    for (uint d = 0; d < n_digits_; ++d) setChar(d, c, false);
}

void SevenSeg::setChar(uint digit, char c, bool dp) {
    if (digit >= n_digits_) return;

    uint8_t pat = segPattern(c) | (dp ? 0x80 : 0);
    uint base   = digit * 8;

    for (int seg = 0; seg < 8; ++seg) {
        bool on = pat & (1u << seg);
        buf_[base + seg] = on ? Ws2812::toGrb(Colour::Green) : 0;
    }
    flush();
}

void SevenSeg::flush() {
    for (uint i = 0; i < n_digits_ * 8; ++i) strip_.putPixel(buf_[i]);
}

void SevenSeg::showNumber(int value) {
    // simple right-aligned decimal
    bool neg = value < 0;
    unsigned u = neg ? -value : value;

    for (int d = n_digits_ - 1; d >= 0; --d) {
        char c = (u || d == n_digits_-1) ? '0' + (u % 10) : ' ';
        setChar(d, c);
        u /= 10;
    }
    if (neg) setChar(0, '-');
}
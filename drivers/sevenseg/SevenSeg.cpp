#include "SevenSeg.hpp"
#include <algorithm>
#include <cstdlib>

// Segment bit masks (a..g); dot is handled separately
static constexpr uint8_t A = 1<<0;
static constexpr uint8_t B = 1<<1;
static constexpr uint8_t C = 1<<2;
static constexpr uint8_t D = 1<<3;
static constexpr uint8_t E = 1<<4;
static constexpr uint8_t F = 1<<5;
static constexpr uint8_t G = 1<<6;

// Standard 7-seg glyphs (common “on = 1”) for 0..9 and minus
uint8_t SevenSeg::glyphFor(int value) {
    switch (value) {
        case 0:  return A|B|C|D|E|F;            // g off
        case 1:  return B|C;
        case 2:  return A|B|D|E|G;
        case 3:  return A|B|C|D|G;
        case 4:  return F|G|B|C;
        case 5:  return A|F|G|C|D;
        case 6:  return A|F|E|D|C|G;
        case 7:  return A|B|C;
        case 8:  return A|B|C|D|E|F|G;
        case 9:  return A|B|C|D|F|G;
        case -1: return G;                       // minus sign
        default: return 0;                       // blank
    }
}

void SevenSeg::clear() {
    // We do not know total strip length here; we only touch mapped pixels
    for (uint8_t d = 0; d < layout_.digits; ++d) {
        for (uint8_t s = 0; s < SEG_COUNT; ++s) {
            strip_.setPixel(pix(d, s), 0, 0, 0);
        }
    }
}

void SevenSeg::show() {
    strip_.show();
}

void SevenSeg::setDigitMask(uint8_t digit, uint8_t segMask, uint8_t r, uint8_t g, uint8_t b) {
    if (digit >= layout_.digits) return;
    // Segments a..g per bit in segMask
    for (uint8_t bit = 0; bit < 7; ++bit) {
        bool on = (segMask >> bit) & 1;
        uint8_t rr = on ? r : 0, gg = on ? g : 0, bb = on ? b : 0;
        strip_.setPixel(pix(digit, bit), rr, gg, bb);
    }
    // Dot is not part of segMask; leave as-is
}

void SevenSeg::setDot(uint8_t digit, bool on, uint8_t r, uint8_t g, uint8_t b) {
    if (digit >= layout_.digits) return;
    uint16_t p = pix(digit, SEG_P);
    if (on) strip_.setPixel(p, r, g, b);
    else    strip_.setPixel(p, 0, 0, 0);
}

void SevenSeg::setDigit(uint8_t digit, int value, uint8_t r, uint8_t g, uint8_t b) {
    if (digit >= layout_.digits) return;
    uint8_t mask = glyphFor(value);
    setDigitMask(digit, mask, r, g, b);
}

void SevenSeg::printNumber(int value, uint8_t r, uint8_t g, uint8_t b, bool leftAlign) {
    // Convert to string with sign
    char buf[16];
    int n = 0;

    if (value < 0) {
        buf[n++] = '-';
        value = -value;
    }

    // Write digits into a temp buffer (reverse)
    char rev[16];
    int rn = 0;
    if (value == 0) rev[rn++] = '0';
    while (value > 0 && rn < 16) {
        rev[rn++] = char('0' + (value % 10));
        value /= 10;
    }

    // Merge sign + digits to forward order
    int total = 0;
    if (n == 1) buf[total++] = '-';
    for (int i = rn-1; i >= 0; --i) buf[total++] = rev[i];

    // Clear all digits first
    for (uint8_t d = 0; d < layout_.digits; ++d) setDigit(d, 99, 0,0,0); // 99 -> blank

    // Decide placement within available digits
    int digCount = layout_.digits;
    int start = 0;
    if (!leftAlign) {
        start = digCount - total;
        if (start < 0) start = 0; // overflow: we’ll clip
    }

    // Draw into digits
    int di = start;
    for (int i = 0; i < total && di < digCount; ++i, ++di) {
        if (buf[i] == '-') setDigit(di, -1, r,g,b);
        else               setDigit(di, buf[i]-'0', r,g,b);
    }
}
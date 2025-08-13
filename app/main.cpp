#include "pico/stdlib.h"
#include "Rotary_Button.hpp"
#include "Ws2812.hpp"
#include "SevenSeg.hpp"

// Provided in your layout .cpp
extern const SevenSegLayout LAYOUT_6DIGITS;

int main() {
    stdio_init_all();
    sleep_ms(300);

    // Rotary button still auto-drives the ring
    Rotary_Button enc;

    // Your seven-seg strip (put its data pin & total LED count)
    constexpr uint SEVENSEG_PIN = 28;
    constexpr uint SEVENSEG_PIX = 48; // 6 digits * 8 segments
    Ws2812 sevenStrip(SEVENSEG_PIN, SEVENSEG_PIX);
    SevenSeg seg(sevenStrip, LAYOUT_6DIGITS);

    // Show a sample value
    seg.clear();
    seg.printNumber(-123456, /*r,g,b=*/40, 10, 0); // warm orange
    seg.setDot(1, true,  40, 10, 0); // dot on digit 1
    seg.setDot(3, true,  40, 10, 0); // dot on digit 3
    seg.show();

    while (true) {
        // keep the ring updated by calling the getters
        (void)enc.getPosition();
        (void)enc.isPressed();
        sleep_ms(5);
    }
}
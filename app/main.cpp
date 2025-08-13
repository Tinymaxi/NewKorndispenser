#include "pico/stdlib.h"
#include "Rotary_Button.hpp"
#include "SevenSeg.hpp"
#include "SevenSegLayout.cpp" // or use the header with extern + compiled SevenSegLayout.cpp

int main() {
    stdio_init_all();

    // Rotary encoder + Neopixel ring
    Rotary_Button enc;

    // Seven segment display (6 digits)
    Ws2812 ws_segments(28, 48); // adjust pin & LED count for your 6-digit setup
    SevenSeg seven(ws_segments, LAYOUT_6DIGITS);

    while (true) {
        // Read encoder position
        int pos = enc.getPosition();

        // Show value on the 7-seg
        seven.clear();
        seven.printNumber(pos, 255, 128, 0); // orange
        seven.show();

        // Small debounce delay
        sleep_ms(20);
    }
}
#include "pico/stdlib.h"
#include "Rotary_Button.hpp"


int main() {
    stdio_init_all();

    // Rotary button controls the ring automatically now
    Rotary_Button enc;

    while (true) {
        int pos = enc.getPosition();     // also updates the ring
        bool pressed = enc.isPressed();  // also updates the ring

        sleep_ms(2);
    }
}
#include "pico/stdlib.h"
#include "Rotary_Button.hpp"
#include <stdio.h>

int main() {
    stdio_init_all();
    sleep_ms(500); // allow USB to enumerate

    printf("Rotary_Button test starting...\n");

    Rotary_Button enc;

    while (true) {
        int pos = enc.getPosition();     // updates ring automatically
        bool pressed = enc.isPressed();  // updates ring automatically

        printf("Position: %d, Pressed: %d\n", pos, pressed);

        // Immediately zero when button pressed
        if (pressed) {
            enc.setZero();
            printf("Encoder reset to zero\n");
        }

        sleep_ms(50);
    }
}
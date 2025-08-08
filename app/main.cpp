#include "pico/stdlib.h"
#include "Rotary_Button.hpp"

int main() {
    stdio_init_all();
    Rotary_Button enc;

    while (true) {
        enc.poll();                  // one cheap call
        // your own logic can query enc.getPosition() / isPressed()
        sleep_ms(5);
    }
}
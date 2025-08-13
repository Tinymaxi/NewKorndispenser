#include "pico/stdlib.h"
#include "Rotary_Button.hpp"
#include <stdio.h>

int main()
{
    stdio_init_all();
    Rotary_Button enc;

    while (true)
    {

        int pos = enc.getPosition();
        printf("Position: %d, Modulo 4: %d, Modulo 10: %d, Modulo 12: %d\n", pos, pos % 4, pos % 10, pos % 12);

        

        if (enc.isPressed())
        {
            enc.setZero();
        }

        sleep_ms(500);
    }
}
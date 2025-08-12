#include "pico/stdlib.h"
#include "Rotary_Button.hpp"
#include <stdio.h>

int main() {
    stdio_init_all();
    Rotary_Button enc;

    while (true) {
        enc.poll(); 
        
        if (enc.isPressed()){
            enc.setZero();
            printf("Encoder reset to zero/n");
        }

        printf("Position: %d , %s \n",
                enc.getPosition(),
                 enc.isPressed() ? "pressed" : "released");  
  

        sleep_ms(5);
    }
}
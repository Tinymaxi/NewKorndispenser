// #include "pico/stdlib.h"
// #include "Rotary_Button.hpp"
// #include "SevenSeg.hpp"
// #include "SevenSegLayout.cpp" // or use the header with extern + compiled SevenSegLayout.cpp

// int main() {
//     stdio_init_all();

//     // Rotary encoder + Neopixel ring
//     Rotary_Button enc;

//     // Seven segment display (6 digits)
//     Ws2812 ws_segments(28, 48); // adjust pin & LED count for your 6-digit setup
//     SevenSeg seven(ws_segments, LAYOUT_6DIGITS);

//     while (true) {
//         // Read encoder position
//         int pos = enc.getPosition();

//         // Show value on the 7-seg
//         seven.clear();
//         seven.printNumber(pos, 255, 128, 0); // orange
//         seven.show();

//         // Small debounce delay
//         sleep_ms(20);
//     }
// }

// #include "pico/stdlib.h"
// #include "Lcd1602I2C.hpp"

// int main()
// {
//     stdio_init_all();

//     // If you don’t know the address, you can scan:
//     // uint8_t addr = Lcd1602I2C::scanFirst(i2c0);
//     // if (addr == 0xFF) { while (true) { sleep_ms(1000); } }

    
//     Lcd1602I2C lcd(i2c0, 0x27, 20, 4); 
//     lcd.init(PICO_DEFAULT_I2C_SDA_PIN, PICO_DEFAULT_I2C_SCL_PIN, 100000);

//     lcd.clear();
//     lcd.setCursor(0, 0);
//     lcd.print("RP2040 Pico");
//     lcd.setCursor(1, 1);
//     lcd.print("I2C LCD readyyyy");

//     lcd.cursor(false);
//     lcd.blink(false);

//     while (true)
//     {
//         tight_loop_contents();
//     }
// }

// #include "pico/stdlib.h"
// #include "Vibrator.hpp"

// int main()
// {
//     stdio_init_all();

//     Vibrator vib(27);

//     while (true)
//     {
//         // ramp up
//         const float MAX_I = 0.35f; // cap
//         for (int i = 0; i <= 100; ++i)
//         {
//             float x = i / 100.0f;
//             vib.setIntensity(x * MAX_I); // 0..0.35
//             sleep_ms(10);
//         }
//         // hold strong
//         sleep_ms(500);

//         // ramp down
//         for (int i = 100; i >= 0; --i)
//         {
//             float x = i / 100.0f;
//             vib.setIntensity(x * MAX_I); // 0..0.35
//             sleep_ms(10);
//         }
//         // pause
//         vib.off();
//         sleep_ms(400);
//     }
// }

#include "pico/stdlib.h"
#include "Servo.hpp"

static void wait_ms_blocking(int ms) { sleep_ms(ms); }

int main() {
    stdio_init_all();

    Servo s(7);     // GPIO 7
    s.center();
    sleep_ms(300);

    while (true) {
        // full-speed jump to 180°
        s.writeDegrees(180.0f);
        wait_ms_blocking(1800);  // give it time to arrive (tune for your linkage)

        // full-speed jump back to 0°
        s.writeDegrees(0.0f);
        wait_ms_blocking(1800);
    }
}
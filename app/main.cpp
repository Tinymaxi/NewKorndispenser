#include "Ws2812.hpp"
#include "NeopixelRing.hpp"

int main() {
    Ws2812 ws(3, 12);
    NeopixelRing ring(ws, 12);

    ring.highlight(1, 0, 0, 255); // pixel 5 blue
    ring.show();

    while (true) {}
}


// #include "pico/stdlib.h"
// #include "Ws2812.hpp"

// int main() {
//     stdio_init_all();

//     Ws2812 ring(/*pin=*/3, /*leds=*/12);
//     ring.clear();
//     ring.setPixel(5, /*r=*/0, /*g=*/0, /*b=*/255); // pixel index 5 -> blue
//     ring.show(); // latch & display

//     while (true) { tight_loop_contents(); }
// }


// #include "pico/stdlib.h"
// #include "Ws2812.hpp"
// #include "Rotary_Button.hpp"

// static inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

// int main() {
//     stdio_init_all();

//     // HW config
//     constexpr uint LED_PIN  = 3;     // your NeoPixel data pin
//     constexpr uint LED_COUNT = 12;   // ring size

//     // Colors (keep modest to limit current)
//     const uint8_t ON_R = 0, ON_G = 40, ON_B = 0;   // green-ish
//     const uint8_t OFF_R = 0, OFF_G = 0, OFF_B = 0;

//     Ws2812 ring(LED_PIN, LED_COUNT);
//     Rotary_Button enc;

//     // State
//     bool inverted = false;
//     bool prevPressed = false;
//     int  lastIdx = -1;

//     // Helper to render based on index + inverted flag
//     auto render = [&](int idx) {
//         // idx expected 0..LED_COUNT-1
//         // In normal mode: only the indexed pixel is ON.
//         // In inverted mode: all pixels except the indexed one are ON.
//         if (!inverted) {
//             ring.clear();
//             ring.setPixel((uint)idx, ON_R, ON_G, ON_B);
//         } else {
//             // fill all ON, then turn the indexed one OFF
//             for (uint i = 0; i < LED_COUNT; ++i) {
//                 ring.setPixel(i, ON_R, ON_G, ON_B);
//             }
//             ring.setPixel((uint)idx, OFF_R, OFF_G, OFF_B);
//         }
//         ring.show();
//     };

//     while (true) {
//         // Read encoder position and map to 0..LED_COUNT-1 safely.
//         int pos = enc.getPosition();                 // your class returns a signed count
//         int idx = pos % (int)LED_COUNT;
//         if (idx < 0) idx += LED_COUNT;               // handle negatives

//         // Button edge detect (active-low assumed in your earlier code)
//         bool pressed = enc.isPressed();
//         bool risingEdge = pressed && !prevPressed;   // change on press, not on release
//         prevPressed = pressed;

//         if (risingEdge) {
//             inverted = !inverted;                    // toggle inversion
//             render(idx);                             // show immediately
//             // simple de-bounce guard
//             sleep_ms(15);
//         }

//         if (idx != lastIdx) {
//             lastIdx = idx;
//             render(idx);
//         }

//         sleep_ms(2);
//     }
// }
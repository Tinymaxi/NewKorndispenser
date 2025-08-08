#include "Rotary_Button.hpp"
#include "quadrature_encoder.pio.h"
#include "hardware/pio.h"

namespace {
inline int detent(int32_t raw) { return int(raw >> 2); }
} // unnamed namespace

Rotary_Button::Rotary_Button() {
    // PIO quadrature
    offset_ = pio_add_program(pio0, &quadrature_encoder_program);
    quadrature_encoder_program_init(pio0, SM_INDEX, PIN_AB, offset_);

    // button
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    setZero();
}

void Rotary_Button::setZero() {
    pio_sm_set_enabled(pio0, SM_INDEX, false);
    pio_sm_exec(pio0, SM_INDEX, pio_encode_set(pio_y, 0));
    pio_sm_exec(pio0, SM_INDEX, pio_encode_set(pio_x, 0));
    pio_sm_exec(pio0, SM_INDEX, pio_encode_push(false, false));
    pio_sm_set_enabled(pio0, SM_INDEX, true);

    s_     = {0, gpio_get(BUTTON_PIN)==0};
    prev_  = {-1,false};
    refreshRing();
}

void Rotary_Button::refreshRing() {
    ring_.update(s_.pos, s_.pressed ? Ws2812::Mode::Inverted
                                    : Ws2812::Mode::Single);
}

void Rotary_Button::poll() {
    s_.pos     = detent(quadrature_encoder_get_count(pio0, SM_INDEX));
    s_.pressed = (gpio_get(BUTTON_PIN) == 0);

    bool invert = s_.pressed;
    if (s_.pos != prev_.pos || invert != prev_.invert) {
        refreshRing();
        prev_ = {s_.pos, invert};
    }
}
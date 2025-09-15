#include "Rotary_Button.hpp"
#include "quadrature_encoder.pio.h"
#include "hardware/pio.h"
#include <stdio.h>

Rotary_Button::Rotary_Button()
{
    // --- Quadrature encoder PIO init (from your old working code) ---
    pio_add_program(pio0, &quadrature_encoder_program);
    // The last parameter to init is the wrap pin for B = A+1
    quadrature_encoder_program_init(pio0, SM_INDEX, PIN_AB, 0);

    // --- Button init ---
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    // --- Start with pixel 0 lit ---
    ring_.clear();
    ring_.setIndex(0, ON_R, ON_G, ON_B);
    ring_.show();
}

int Rotary_Button::getPosition()
{
    int pos = readPositionRaw_();
    bool pressed = readPressedRaw_();
    updateRing_(pos, pressed);
    return pos;
}

void Rotary_Button::setZero()
{
    pio_sm_set_enabled(pio0, SM_INDEX, false);
    pio_sm_exec(pio0, SM_INDEX, pio_encode_set(pio_y, 0));
    pio_sm_exec(pio0, SM_INDEX, pio_encode_set(pio_x, 0));
    pio_sm_exec(pio0, SM_INDEX, pio_encode_push(false, false));
    pio_sm_set_enabled(pio0, SM_INDEX, true);
}

bool Rotary_Button::isPressed()
{
    printf("In Pressed");
    bool pressed = readPressedRaw_();
    pressed &= gpio_get(BUTTON_PIN);
    printf("isPressed() -> %d\r\n", pressed);
    int pos = readPositionRaw_();
    updateRing_(pos, pressed);
    return pressed;
}

void Rotary_Button::updateRing_(int pos, bool pressed)
{
    uint8_t idx = wrap_index(pos, (uint8_t)RING_COUNT);

    if ((int)idx == last_idx_ && pressed == last_pressed_)
        return;

    last_idx_ = idx;
    last_pressed_ = pressed;

    if (!pressed)
    {
        ring_.clear();
        ring_.setIndex(idx, ON_R, ON_G, 60); // 60 gives a nice violet.
    }
    else
    {
        ring_.fill(ON_R, ON_G, ON_B);
        ring_.setIndex(idx, OFF_R, OFF_G, OFF_B);
    }
    ring_.show();
}

int Rotary_Button::readPositionRaw_() const
{
    int count = quadrature_encoder_get_count(pio0, SM_INDEX);
    printf("Raw count: %d\n", count); // optional debug
    return count / POSITION_DIVISOR;
}

bool Rotary_Button::readPressedRaw_() const {
    return gpio_get(BUTTON_PIN) == 0;   
}
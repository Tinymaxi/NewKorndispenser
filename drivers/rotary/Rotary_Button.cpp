#include "Rotary_Button.hpp"
#include "quadrature_encoder.pio.h"
#include "hardware/pio.h"
#include <stdio.h>

Rotary_Button::Rotary_Button()
{
    // PIO quadrature
    pio_add_program(pio0, &quadrature_encoder_program);
    quadrature_encoder_program_init(pio0, SM_INDEX, PIN_AB, 0);

    // button
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);
}

int Rotary_Button::getPosition()
{
    return quadrature_encoder_get_count(pio0, SM_INDEX) / POSITION_DIVISOR;
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
    int buttonState = gpio_get(BUTTON_PIN);
    printf("Button state: %d\n", buttonState);
    return buttonState == 0;
}

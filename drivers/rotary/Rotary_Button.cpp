#include "Rotary_Button.hpp"
#include "quadrature_encoder.pio.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include <stdio.h>

Rotary_Button::Rotary_Button()
{
    // Use PIO1 instead of PIO0 to avoid potential conflicts
    PIO pio = pio1;

    // --- Quadrature encoder PIO init (requires address 0) ---
    uint offset = pio_add_program(pio, &quadrature_encoder_program);
    hard_assert(offset == 0);  // Program must be at address 0

    // --- Manual PIO init with RP2350 fixes ---
    pio_sm_set_consecutive_pindirs(pio, SM_INDEX, PIN_AB, 2, false);

    pio_sm_config c = quadrature_encoder_program_get_default_config(offset);
    sm_config_set_in_pins(&c, PIN_AB);
    sm_config_set_jmp_pin(&c, PIN_AB);
    sm_config_set_in_shift(&c, false, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_NONE);
    sm_config_set_clkdiv(&c, 1.0f);

    // Initialize SM but don't enable yet
    pio_sm_init(pio, SM_INDEX, offset, &c);

    // RP2350: Set GPIO function to PIO1 AND enable input
    pio_gpio_init(pio, PIN_AB);
    pio_gpio_init(pio, PIN_AB + 1);
    gpio_set_input_enabled(PIN_AB, true);
    gpio_set_input_enabled(PIN_AB + 1, true);
    gpio_pull_up(PIN_AB);
    gpio_pull_up(PIN_AB + 1);

    // Pre-initialize state machine registers
    uint32_t pins = (gpio_get(PIN_AB) ? 1 : 0) | (gpio_get(PIN_AB + 1) ? 2 : 0);
    pio_sm_exec(pio, SM_INDEX, pio_encode_set(pio_x, pins));
    pio_sm_exec(pio, SM_INDEX, pio_encode_mov(pio_osr, pio_x));
    pio_sm_exec(pio, SM_INDEX, pio_encode_set(pio_y, 0));

    // Store PIO reference for later use
    encoder_pio_ = pio;

    // Now enable the state machine
    pio_sm_set_enabled(pio, SM_INDEX, true);

    // --- Button init ---
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    // --- Now create ring LED (after encoder PIO is set up) ---
    ws_ = new Ws2812(RING_PIN, RING_COUNT);
    ring_ = new NeopixelRing(*ws_, (uint8_t)RING_COUNT);

    // --- Start with pixel 0 lit ---
    ring_->clear();
    ring_->setIndex(0, ON_R, ON_G, ON_B);
    ring_->show();
}

int Rotary_Button::getPosition()
{
    int raw_pos = readPositionRaw_();
    bool pressed = readPressedRaw_();

    // Time-based debounce: only update position if enough time has passed
    if (raw_pos != debounced_pos_) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_change_time_ >= DEBOUNCE_MS) {
            debounced_pos_ = raw_pos;
            last_change_time_ = now;
        }
    }

    updateRing_(debounced_pos_, pressed);
    return debounced_pos_;
}

void Rotary_Button::setZero()
{
    pio_sm_set_enabled(encoder_pio_, SM_INDEX, false);
    pio_sm_exec(encoder_pio_, SM_INDEX, pio_encode_set(pio_y, 0));
    pio_sm_exec(encoder_pio_, SM_INDEX, pio_encode_set(pio_x, 0));
    pio_sm_exec(encoder_pio_, SM_INDEX, pio_encode_push(false, false));
    pio_sm_set_enabled(encoder_pio_, SM_INDEX, true);
    debounced_pos_ = 0;
}

bool Rotary_Button::isPressed()
{
    return gpio_get(BUTTON_PIN) == 0;
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
        ring_->clear();
        ring_->setIndex(idx, ON_R, ON_G, 60); // 60 gives a nice violet.
    }
    else
    {
        ring_->fill(ON_R, ON_G, ON_B);
        ring_->setIndex(idx, OFF_R, OFF_G, OFF_B);
    }
    ring_->show();
}

int Rotary_Button::readPositionRaw_() const
{
    return quadrature_encoder_get_count(encoder_pio_, SM_INDEX) / POSITION_DIVISOR;
}

bool Rotary_Button::readPressedRaw_() const {
    return gpio_get(BUTTON_PIN) == 0;   
}
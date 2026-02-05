#include "Rotary_Button.hpp"
#include "quadrature_encoder.pio.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include <stdio.h>
#include <cmath>

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
    uint32_t now = to_ms_since_boot(get_absolute_time());

    // Track when position changes
    if (raw_pos != debounced_pos_) {
        if (now - last_change_time_ >= DEBOUNCE_MS) {
            debounced_pos_ = raw_pos;
            last_change_time_ = now;
            last_move_time_ = now;  // Position just changed
        }
    }

    // Only update stable position if current position has been stable for STABLE_MS
    if (now - last_move_time_ >= STABLE_MS) {
        stable_pos_ = debounced_pos_;
    }

    updateRing_(debounced_pos_, pressed);

    // Return stable position (immune to recent slips)
    return stable_pos_;
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
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint8_t idx = wrap_index(pos, (uint8_t)RING_COUNT);

    // Detect turn direction
    if (pos != prev_pos_) {
        turn_direction_ = (pos > prev_pos_) ? 1 : -1;
        prev_pos_ = pos;
    }

    // Detect button press start (for ripple animation)
    if (pressed && !last_pressed_) {
        press_start_time_ = now;
        press_anim_active_ = true;
    }
    last_pressed_ = pressed;
    last_idx_ = idx;

    ring_->clear();

    uint32_t since_move = now - last_move_time_;
    bool idle = (since_move > IDLE_AFTER_MS);

    // --- PRESS ANIMATION: Expanding ripple ---
    if (press_anim_active_) {
        uint32_t elapsed = now - press_start_time_;
        if (elapsed < PRESS_ANIM_MS) {
            // Ripple expands from 0 to 6 LEDs on each side
            float progress = (float)elapsed / PRESS_ANIM_MS;
            int ripple_radius = (int)(progress * 6);

            // Flash bright at start, then fade
            float intensity = 1.0f - (progress * 0.7f);

            for (int offset = -ripple_radius; offset <= ripple_radius; offset++) {
                uint8_t ring_idx = wrap_index(pos + offset, (uint8_t)RING_COUNT);
                // Edge of ripple is brightest
                float edge_dist = (float)abs(abs(offset) - ripple_radius);
                float brightness = intensity * (1.0f - edge_dist * 0.3f);
                if (brightness < 0.1f) brightness = 0.1f;

                // Bright white/purple flash
                uint8_t r = (uint8_t)(255 * brightness);
                uint8_t g = (uint8_t)(150 * brightness);
                uint8_t b = (uint8_t)(255 * brightness);
                ring_->setIndex(ring_idx, r, g, b);
            }
            ring_->show();
            return;
        } else {
            press_anim_active_ = false;
        }
    }

    // --- BUTTON HELD: All LEDs lit bright with current position dark ---
    if (pressed) {
        ring_->fill(80, 30, 100);  // Brighter purple when held
        ring_->setIndex(idx, 0, 0, 0);
        ring_->show();
        return;
    }

    // --- IDLE: Dramatic pulse/breathing effect ---
    if (idle) {
        // Sine wave for smooth breathing (0.05 to 1.0 range - very dim to bright)
        float phase = (float)(now % PULSE_PERIOD_MS) / PULSE_PERIOD_MS;
        float breath = 0.05f + 0.95f * (0.5f + 0.5f * sinf(phase * 2.0f * 3.14159f));

        // Brighter base colors for more visible pulse
        uint8_t r = (uint8_t)(80 * breath);
        uint8_t g = (uint8_t)(30 * breath);
        uint8_t b = (uint8_t)(120 * breath);  // More purple/violet

        ring_->setIndex(idx, r, g, b);
        ring_->show();
        return;
    }

    // --- TURNING: Comet trail effect (4 LEDs total) ---
    // Main LED at full brightness - bright violet
    ring_->setIndex(idx, 80, 30, 120);

    // Trail of 3 LEDs with sharp falloff
    const float trail_fade[] = {0.35f, 0.12f, 0.03f};  // Much sharper falloff
    for (int i = 1; i <= 3; i++) {
        int trail_pos = pos - (turn_direction_ * i);
        uint8_t trail_idx = wrap_index(trail_pos, (uint8_t)RING_COUNT);
        float fade = trail_fade[i - 1];

        uint8_t r = (uint8_t)(80 * fade);
        uint8_t g = (uint8_t)(30 * fade);
        uint8_t b = (uint8_t)(120 * fade);
        ring_->setIndex(trail_idx, r, g, b);
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
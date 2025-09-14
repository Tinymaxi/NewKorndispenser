#include "Vibrator.hpp"
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include <cmath>

static inline float clamp01(float v){ return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

Vibrator::Vibrator(int gpio_pwm) : _gpio(gpio_pwm) {
    gpio_set_function(_gpio, GPIO_FUNC_PWM);
    _slice   = pwm_gpio_to_slice_num(_gpio);
    _channel = pwm_gpio_to_channel(_gpio);
    configurePwm20kHz();
    off();
}

void Vibrator::configurePwm20kHz() {
    // f = clk_sys / (div * (TOP+1))
    // For 20 kHz with clk=125 MHz: choose div=1.0, TOP ≈ 125e6/20e3 - 1 = 6249
    const uint32_t clk = clock_get_hz(clk_sys);
    float div = 1.0f;
    uint32_t top_calc = (clk / (uint32_t)20000) - 1u;
    if (top_calc > 65535u) {         // fallback if clk differs
        div = (float)clk / (20000.0f * 65536.0f);
        if (div < 1.0f) div = 1.0f;
        if (div > 255.0f) div = 255.0f;
        top_calc = (uint32_t)((clk / div) / 20000.0f) - 1u;
        if (top_calc > 65535u) top_calc = 65535u;
    }
    _top = (uint16_t)top_calc;

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, div);
    pwm_config_set_wrap(&cfg, _top);
    pwm_init(_slice, &cfg, true);
}

void Vibrator::setIntensity(float intensity) {
    intensity = clamp01(intensity);
    // duty counts = intensity * (TOP+1)
    uint16_t level = (uint16_t)((_top + 1u) * intensity);
    pwm_set_chan_level(_slice, _channel, level);
    pwm_set_enabled(_slice, true);
}
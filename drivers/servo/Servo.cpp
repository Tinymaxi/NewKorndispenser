#include "Servo.hpp"
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include <cmath>
#include <algorithm>

static inline uint16_t clamp_u16(int v, int lo, int hi) {
    return (uint16_t)std::min(hi, std::max(lo, v));
}

Servo::Servo(int gpio) : _gpio(gpio) {
    gpio_set_function(_gpio, GPIO_FUNC_PWM);
    _slice   = pwm_gpio_to_slice_num(_gpio);
    _channel = pwm_gpio_to_channel(_gpio);
    setupPwmFixed();
    center();
}

void Servo::setupPwmFixed() {
    // Target: 333 Hz frame rate.
    // Use f_out = clk_sys / (div * (TOP+1)).
    // Aim for ~1 MHz PWM clock so 1 count ≈ 1 µs (good resolution).
    const double clk = (double)clock_get_hz(clk_sys);   // ~125e6 on Pico by default

    double div = clk / 1'000'000.0;                     // ~125.0
    if (div < 1.0)   div = 1.0;
    if (div > 255.0) div = 255.0;

    // TOP = clk/(div*frame) - 1
    double top_d = (clk / (div * (double)FRAME_HZ)) - 1.0;
    if (top_d > 65535.0) top_d = 65535.0;
    if (top_d < 1000.0)  top_d = 1000.0;                // keep some resolution

    _top = (uint16_t)llround(top_d);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, (float)div);
    pwm_config_set_wrap(&cfg, _top);
    pwm_init(_slice, &cfg, true);
}

void Servo::applyPulseUs(uint16_t us) {
    // period_us = 1e6 / FRAME_HZ ≈ 3003.003...
    const double period_us = 1e6 / (double)FRAME_HZ;

    // counts = us * (TOP+1) / period_us
    uint32_t counts = (uint32_t)llround(((double)us * (double)(_top + 1)) / period_us);
    if (counts > _top) counts = _top;

    pwm_set_chan_level(_slice, _channel, (uint16_t)counts);
    pwm_set_enabled(_slice, true);
}

void Servo::writeMicros(uint16_t us) {
    us = clamp_u16(us, US_MIN, US_MAX);
    applyPulseUs(us);
}

void Servo::center() {
    writeMicros(US_CENT);
}

void Servo::writeDegrees(float deg) {
    if (deg < 0.0f)   deg = 0.0f;
    if (deg > 180.0f) deg = 180.0f;
    // Linear map 0..180° -> 800..2200 µs
    float us = (float)US_MIN + (deg / 180.0f) * (float)(US_MAX - US_MIN);
    writeMicros((uint16_t)llround(us));
}
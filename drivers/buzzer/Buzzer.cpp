#include "Buzzer.hpp"

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include <algorithm>
#include <cmath>

// Clamp helper
static inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

Buzzer::Buzzer(int gpio_buzzer)
: _gpio(gpio_buzzer)
{
    gpio_set_function(_gpio, GPIO_FUNC_PWM);
    _slice   = pwm_gpio_to_slice_num(_gpio);
    _channel = pwm_gpio_to_channel(_gpio);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 1.0f);
    pwm_init(_slice, &cfg, true);

    pwm_set_gpio_level(_gpio, 0); // silent start
}

void Buzzer::startTone(uint32_t freq_hz, float volume)
{
    volume = clamp01(volume);

    if (freq_hz == 0) {
        stopTone();
        return;
    }

    const double clk = (double)clock_get_hz(clk_sys);

    // pick divider so TOP fits in 16-bit
    double div_d = ceil(clk / ((double)freq_hz * 65536.0));
    if (div_d < 1.0) div_d = 1.0;
    if (div_d > 255.0) div_d = 255.0;

    double top_d = (clk / (div_d * (double)freq_hz)) - 1.0;

    // improve resolution if top is too small
    if (top_d < 200.0 && div_d < 255.0) {
        double div_target = ceil(clk / ((double)freq_hz * 400.0));
        if (div_target >= 1.0 && div_target <= 255.0) {
            div_d = div_target;
            top_d = (clk / (div_d * (double)freq_hz)) - 1.0;
        }
    }

    if (top_d > 65535.0) top_d = 65535.0;
    if (top_d < 1.0)     top_d = 1.0;

    uint16_t top = (uint16_t)llround(top_d);
    pwm_set_clkdiv(_slice, (float)div_d);
    pwm_set_wrap(_slice, top);

    uint16_t level = (uint16_t)(((uint32_t)top + 1u) * 0.5f * volume);
    pwm_set_chan_level(_slice, _channel, level);
    pwm_set_enabled(_slice, true);
}

void Buzzer::stopTone()
{
    pwm_set_chan_level(_slice, _channel, 0);
    pwm_set_enabled(_slice, true);
}

void Buzzer::playTone(uint32_t freq_hz, uint32_t duration_ms, float volume)
{
    startTone(freq_hz, volume);
    sleep_ms(duration_ms);
    stopTone();
}

void Buzzer::rest(uint32_t duration_ms)
{
    stopTone();
    sleep_ms(duration_ms);
}

void Buzzer::beep(uint32_t duration_ms, float volume)
{
    const uint32_t f1 = 1400, f2 = 1800;
    playTone(f1, duration_ms, volume);
    rest(duration_ms / 3);
    playTone(f2, duration_ms, volume);
}

void Buzzer::playMelody(const std::vector<Note>& melody, uint32_t gap_ms, float volume)
{
    for (auto& n : melody) {
        if (n.freq_hz == 0) rest(n.duration_ms);
        else playTone(n.freq_hz, n.duration_ms, volume);
        if (gap_ms) rest(gap_ms);
    }
}

// --- Note frequencies ---
namespace {
constexpr uint32_t NOTE_C4=261,  NOTE_D4=293,  NOTE_E4=329,  NOTE_F4=349,
                   NOTE_G4=392,  NOTE_A4=440,  NOTE_B4=493;
constexpr uint32_t NOTE_C5=523,  NOTE_D5=587,  NOTE_E5=659,  NOTE_F5=698,
                   NOTE_G5=784,  NOTE_A5=880,  NOTE_B5=987;

static inline uint32_t dur_quarter(uint32_t bpm) {
    return 60000u / std::max<uint32_t>(bpm, 1);
}
}

std::vector<Buzzer::Note> Buzzer::melodyOdeToJoy(uint32_t tempo_bpm)
{
    const uint32_t q = dur_quarter(tempo_bpm);
    const uint32_t h = 2*q;
    const uint32_t e = q/2;

    return {
        {NOTE_E4, q}, {NOTE_E4, q}, {NOTE_F4, q}, {NOTE_G4, q},
        {NOTE_G4, q}, {NOTE_F4, q}, {NOTE_E4, q}, {NOTE_D4, q},
        {NOTE_C4, q}, {NOTE_C4, q}, {NOTE_D4, q}, {NOTE_E4, q},
        {NOTE_E4, e}, {NOTE_D4, e}, {NOTE_D4, h},
        {NOTE_E4, q}, {NOTE_E4, q}, {NOTE_F4, q}, {NOTE_G4, q},
        {NOTE_G4, q}, {NOTE_F4, q}, {NOTE_E4, q}, {NOTE_D4, q},
        {NOTE_C4, q}, {NOTE_C4, q}, {NOTE_D4, q}, {NOTE_E4, q},
        {NOTE_D4, q}, {NOTE_C4, q}, {NOTE_C4, h}
    };
}

std::vector<Buzzer::Note> Buzzer::melodyTwinkle(uint32_t tempo_bpm)
{
    const uint32_t q = dur_quarter(tempo_bpm);
    const uint32_t h = 2*q;

    return {
        {NOTE_C4, q}, {NOTE_C4, q}, {NOTE_G4, q}, {NOTE_G4, q},
        {NOTE_A4, q}, {NOTE_A4, q}, {NOTE_G4, h},
        {NOTE_F4, q}, {NOTE_F4, q}, {NOTE_E4, q}, {NOTE_E4, q},
        {NOTE_D4, q}, {NOTE_D4, q}, {NOTE_C4, h},
        {NOTE_G4, q}, {NOTE_G4, q}, {NOTE_F4, q}, {NOTE_F4, q},
        {NOTE_E4, q}, {NOTE_E4, q}, {NOTE_D4, h},
        {NOTE_G4, q}, {NOTE_G4, q}, {NOTE_F4, q}, {NOTE_F4, q},
        {NOTE_E4, q}, {NOTE_E4, q}, {NOTE_D4, h},
        {NOTE_C4, q}, {NOTE_C4, q}, {NOTE_G4, q}, {NOTE_G4, q},
        {NOTE_A4, q}, {NOTE_A4, q}, {NOTE_G4, h},
        {NOTE_F4, q}, {NOTE_F4, q}, {NOTE_E4, q}, {NOTE_E4, q},
        {NOTE_D4, q}, {NOTE_D4, q}, {NOTE_C4, h}
    };
}
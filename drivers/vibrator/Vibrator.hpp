#pragma once
#include <cstdint>
#include "SharedSlice.hpp"

class Vibrator : public SliceDutyClient {
public:
    explicit Vibrator(int gpio_pwm = 6);   // default GPIO 6
    // intensity in [0.0 .. 1.0]
    void setIntensity(float intensity);
    // quick helper: on/off
    void on()  { setIntensity(1.0f); }
    void off() { setIntensity(0.0f); }

    // Opt-in: when this vibrator shares its PWM slice with a servo, link them so the
    // vibrator adapts its duty to the current slice frequency. Unlinked vibrators
    // behave exactly as before.
    void attachShared(SharedSlice* s);

    // SliceDutyClient: recompute our level for a new slice wrap (freq change).
    void reapplyAgainstTop(uint16_t top) override;

private:
    void configurePwm20kHz();
    void applyLevel(uint16_t top);   // set channel level for _intensity against `top`
    int _gpio;
    int _slice;
    int _channel;
    float _div = 1.0f;               // cached clock divider (for the shared coordinator)
    uint16_t _top;                   // PWM wrap for 20 kHz
    float _intensity = 0.0f;         // last commanded intensity [0..1]
    SharedSlice* _shared = nullptr;  // null = standalone slice, current behavior
};
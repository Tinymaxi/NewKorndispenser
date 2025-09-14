#pragma once
#include <cstdint>

class Vibrator {
public:
    explicit Vibrator(int gpio_pwm = 6);   // default GPIO 6
    // intensity in [0.0 .. 1.0]
    void setIntensity(float intensity);
    // quick helper: on/off
    void on()  { setIntensity(1.0f); }
    void off() { setIntensity(0.0f); }

private:
    void configurePwm20kHz();
    int _gpio;
    int _slice;
    int _channel;
    uint16_t _top; // PWM wrap for 20 kHz
};
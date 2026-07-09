#pragma once
#include <cstdint>

class SharedSlice;   // coordinator for a PWM slice shared with a vibrator

class Servo {
public:
    explicit Servo(int gpio = 7);   // signal pin (defaults to GPIO 7)

    void writeMicros(uint16_t us);  // 800..2200 µs (clamped)
    void writeDegrees(float deg);   // 0..180°   (maps to 800..2200 µs)
    void center();                  // 1520 µs
    void off();                     // Stop PWM signal (servo relaxes, no holding torque)

    // Opt-in: when this servo shares its PWM slice with a vibrator, link them so the
    // slice runs at the servo's 333 Hz whenever the servo is driven. Unlinked servos
    // behave exactly as before.
    void attachShared(SharedSlice* s);

private:
    void setupPwmFixed();           // 333 Hz
    void applyPulseUs(uint16_t us);

    int _gpio;
    int _slice;
    int _channel;
    float _div = 1.0f;              // cached clock divider (for the shared coordinator)
    SharedSlice* _shared = nullptr; // null = standalone slice, current behavior

    // Fixed specs (Savöx SH-1290MG)
    static constexpr float FRAME_HZ   = 333.0f;
    static constexpr uint16_t US_MIN  = 800;
    static constexpr uint16_t US_MAX  = 2200;
    static constexpr uint16_t US_CENT = 1520;

    // Cached PWM wrap value
    uint16_t _top = 0;
};
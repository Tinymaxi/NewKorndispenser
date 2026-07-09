#include "SharedSlice.hpp"
#include "hardware/pwm.h"

void SharedSlice::registerServo(int slice, float div, uint16_t top) {
    _slice    = slice;
    _servoDiv = div;
    _servoTop = top;
}

void SharedSlice::registerVib(SliceDutyClient* client, int slice, float div, uint16_t top) {
    _slice     = slice;
    _vibClient = client;
    _vibDiv    = div;
    _vibTop    = top;
    // After the drivers' constructors run, the vibrator ctor was last to configure
    // this slice, leaving it at 20 kHz. Reflect that so the first servo write is
    // correctly detected as a transition.
    _mode       = Mode::Vib20k;
    _currentTop = top;
}

void SharedSlice::setServo333() {
    pwm_set_clkdiv((uint)_slice, _servoDiv);
    pwm_set_wrap((uint)_slice, _servoTop);
    _mode       = Mode::Servo333;
    _currentTop = _servoTop;
}

void SharedSlice::setVib20k() {
    pwm_set_clkdiv((uint)_slice, _vibDiv);
    pwm_set_wrap((uint)_slice, _vibTop);
    _mode       = Mode::Vib20k;
    _currentTop = _vibTop;
}

void SharedSlice::onServoActive() {
    _servoActive = true;
    if (_mode != Mode::Servo333) {
        setServo333();
        // The vibrator's duty was scaled against the old (20 kHz) TOP; rescale it
        // for the new one so its intensity stays correct (it now hums at 333 Hz).
        if (_vibClient) _vibClient->reapplyAgainstTop(_currentTop);
    }
}

void SharedSlice::onServoIdle() {
    _servoActive = false;
    if (_mode != Mode::Vib20k) {
        setVib20k();
        if (_vibClient) _vibClient->reapplyAgainstTop(_currentTop);
    }
}

void SharedSlice::vibWantsRun() {
    // While the servo is idle, keep the vibrator quiet at 20 kHz. While the servo is
    // active the slice stays at 333 Hz (the vibrator runs, audibly humming). The
    // caller writes its own level immediately after, so no reapply is needed here.
    if (!_servoActive && _mode != Mode::Vib20k) {
        setVib20k();
    }
}

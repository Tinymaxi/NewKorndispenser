#pragma once
#include <cstdint>

// Interface for a PWM channel whose duty (level) must be recomputed whenever the
// shared slice's wrap (TOP) changes - i.e. when the slice frequency is switched.
class SliceDutyClient {
public:
    virtual void reapplyAgainstTop(uint16_t top) = 0;
protected:
    ~SliceDutyClient() = default;
};

// Coordinates the single PWM frequency of one hardware slice that is shared by a
// servo (needs ~333 Hz) and a vibrator (prefers ~20 kHz for silent switching).
//
// On the RP2040 both channels of a slice share one clock divider and wrap, so they
// are forced to the same frequency. A servo pulse of 800-2200 us cannot exist at
// 20 kHz (period 50 us), so the servo can *only* run at 333 Hz and therefore wins
// whenever it is active. The vibrator tolerates either frequency (it just hums at
// 333 Hz), so it adapts its duty to whatever the slice currently runs at.
//
// Rule: slice = 333 Hz whenever the servo is active, 20 kHz otherwise.
class SharedSlice {
public:
    SharedSlice() = default;

    // Registration - called from the drivers' attachShared(). The drivers pass the
    // timing they already computed in their constructors so there is a single source
    // of truth per device and no duplicated frequency math.
    void registerServo(int slice, float div, uint16_t top);
    void registerVib(SliceDutyClient* client, int slice, float div, uint16_t top);

    // Called by the servo around its output.
    void onServoActive();  // force 333 Hz before the servo writes its pulse
    void onServoIdle();    // hand the slice back to 20 kHz if the vibrator is running

    // Called by the vibrator before it writes its level.
    void vibWantsRun();    // ensure 20 kHz while the servo is idle

    uint16_t currentTop() const { return _currentTop; }
    bool servoActive() const { return _servoActive; }

private:
    enum class Mode { Vib20k, Servo333 };
    void setServo333();
    void setVib20k();

    int      _slice     = -1;
    float    _servoDiv  = 1.0f;
    uint16_t _servoTop  = 0;
    float    _vibDiv    = 1.0f;
    uint16_t _vibTop    = 0;
    SliceDutyClient* _vibClient = nullptr;

    Mode     _mode        = Mode::Vib20k;
    uint16_t _currentTop  = 0;
    bool     _servoActive = false;
};

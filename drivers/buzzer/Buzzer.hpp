#pragma once
#include <cstdint>
#include <vector>

class Buzzer {
public:
    explicit Buzzer(int gpio_buzzer = 2);

    // Simple short beep for button press feedback
    void beep(uint32_t duration_ms = 60, float volume = 0.6f);

    // Play a specific tone (blocking for duration_ms)
    void playTone(uint32_t freq_hz, uint32_t duration_ms, float volume = 0.6f);

    // Rest/silence (blocking)
    void rest(uint32_t duration_ms);

    // Melody item: freq_hz == 0 means rest
    struct Note { uint32_t freq_hz; uint32_t duration_ms; };

    // Play a melody (blocking). Optional gap between notes in ms.
    void playMelody(const std::vector<Note>& melody, uint32_t gap_ms = 10, float volume = 0.6f);

    // Built-in public-domain melodies
    static std::vector<Note> melodyOdeToJoy(uint32_t tempo_bpm = 120);
    static std::vector<Note> melodyTwinkle(uint32_t tempo_bpm = 100);

private:
    void startTone(uint32_t freq_hz, float volume);
    void stopTone();

    int _gpio;
    int _slice;
    int _channel;
};
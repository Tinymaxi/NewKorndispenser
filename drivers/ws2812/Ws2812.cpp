#include "Ws2812.hpp"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "ws2812.pio.h"

Ws2812::Ws2812(uint pin, uint leds)
: pin_(pin), leds_(leds), buf_(leds, 0)
{
    // Claim free PIO/SM and add program for this pin
    bool ok = pio_claim_free_sm_and_add_program_for_gpio_range(
        &ws2812_program, &pio_, &sm_, &offset_, pin_, /*pin_count*/1, /*prefer_pio1*/true
    );
    hard_assert(ok);

    // 800 kHz, RGB (not RGBW)
    ws2812_program_init(pio_, sm_, offset_, pin_, 800000, /*is_rgbw*/false);

    // Enable SM
    pio_sm_set_enabled(pio_, sm_, true);
}

void Ws2812::setPixel(uint i, uint8_t r, uint8_t g, uint8_t b) {
    if (i < leds_) buf_[i] = pack_grb(r, g, b);
}

void Ws2812::clear() {
    std::fill(buf_.begin(), buf_.end(), 0u);
}

void Ws2812::show() {
    // Skip identical frames. Callers repaint every UI tick; retransmitting an
    // unchanged frame only creates flicker windows.
    if (ever_shown_ && buf_ == last_) return;

    // Transmit atomically: an interrupt pausing the stream >50us mid-frame
    // (e.g. WiFi/lwIP work while using the web app) makes the strip latch a
    // partial frame - visible as flicker. 48 LEDs take ~1.5 ms.
    uint32_t irq = save_and_disable_interrupts();
    for (uint i = 0; i < leds_; ++i) {
        pio_sm_put_blocking(pio_, sm_, buf_[i] << 8u);
    }
    restore_interrupts(irq);
    sleep_us(80); // >50µs reset/latch
    last_ = buf_;
    ever_shown_ = true;
}
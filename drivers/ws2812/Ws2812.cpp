#include "Ws2812.hpp"
#include "ws2812.pio.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

uint32_t Ws2812::toGrb(Colour c) {
    switch (c) {
        case Colour::Red:   return 0x00FF00;
        case Colour::Green: return 0xFF0000;
        case Colour::Blue:  return 0x0000FF;
        // case Colour::Orange: return 0xFF8C00;
        default:            return 0;
    }
}

Ws2812::Ws2812(uint pin, uint leds) : n_leds_(leds) {
    offset_ = pio_add_program(pio1, &ws2812_program);

    pio_sm_config cfg = ws2812_program_get_default_config(offset_);
    sm_config_set_sideset_pins(&cfg, pin);
    sm_config_set_out_shift (&cfg, false, true, 24);
    sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_TX);

    const int cycles = ws2812_T1 + ws2812_T2 + ws2812_T3;
    const float div  = (float)clock_get_hz(clk_sys) / (freq * cycles);
    sm_config_set_clkdiv(&cfg, div);

    pio_gpio_init(pio1, pin);
    pio_sm_set_consecutive_pindirs(pio1, sm, pin, 1, true);
    pio_sm_init(pio1, sm, offset_, &cfg);
    pio_sm_set_enabled(pio1, sm, true);

    update(0, Mode::Single, Colour::Off);   // clear ring
}

void Ws2812::putPixel(uint32_t grb) {
    while (pio_sm_is_tx_fifo_full(pio1, sm)) tight_loop_contents();
    pio_sm_put_blocking(pio1, sm, grb << 8u);   // 24→32
}

void Ws2812::update(uint index, Mode mode, Colour colour) {
    index %= n_leds_;
    uint32_t fg = toGrb(colour), bg = 0;

    bool invert = (mode == Mode::Inverted);
    for (uint i = 0; i < n_leds_; ++i)
        putPixel((i == index) ^ invert ? fg : bg);
}
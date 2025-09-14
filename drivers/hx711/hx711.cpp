// hx711.cpp - hx711 driver for Raspberry Pi Pico
// Copyright (c) 2025 Max Penfold
// MIT License
//
// Portions derived from hx711 Arduino library
// Copyright (c) 2019-2025 Rob Tillaart
// MIT License

#include "hx711.hpp"
#include <cstdio>
#include "pico/time.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hx711_reader.pio.h"
#include <algorithm>
#include <numeric>

hx711::hx711(uint clockPin, uint dataPin)
    : clockPin_(clockPin), dataPin_(dataPin)
{
    // Add the program once on this PIO
    if (!programLoaded)
    {
        programOffset = pio_add_program(pio_, &hx711_reader_program);
        programLoaded = true;
    }
    sm_ = pio_claim_unused_sm(pio_, true);
    hx711_reader_pio_init(pio_, sm_, programOffset, dataPin_, clockPin_);
    hx711_reader_program_init(pio_, sm_, programOffset, dataPin_, clockPin_);
}

///////////////////////////////////////////////////////////////
//
//  READ
//
//  From datasheet page 4
int32_t hx711::read_raw_hx711()
{
    uint32_t raw = pio_sm_get_blocking(pio_, sm_);

    // Sign-extend 24 → 32 bits
    if (raw & 0x00800000)
    {
        raw |= 0xFF000000;
    }

    return static_cast<int32_t>(raw);
}

// FIFO discard before reading average.
float hx711::calibr_read_average(uint8_t times)
{
    const uint32_t discardReads = 6;
    int64_t sum = 0;                 // accumulate in wider int
    for (uint8_t i = 0; i < times + discardReads; i++)
    {
        int32_t v = read_raw_hx711();   
        if (i >= discardReads) sum += v;
    }
    float calibration = (float)sum / (float)times;
    printf("Calibration read average : %.6f\n", calibration);
    return (float)sum / (float)times;
}

///////////////////////////////////////////////////////
//
//  MODE
//
// read_weight(): NO FIFO discard, just a light average for responsiveness
float hx711::read_weight(int samples /*=1*/)
{
    if (samples < 1) samples = 1;

    int64_t sum = 0;
    for (int i = 0; i < samples; ++i)
        sum += (int64_t)read_raw_hx711();

    int32_t avg = (int32_t)(sum / samples);
    int32_t net = avg - offset_;
    return (float)net / scale_cpg_;    // counts-per-gram
}

// Configure trimmed moving average parameters
void hx711::set_trimmed_mavg_params(uint8_t window, uint8_t trim_each_side)
{
    if (window == 0) window = 1;
    if (window > TMA_MAX_WINDOW) window = TMA_MAX_WINDOW;
    if (2 * trim_each_side >= window) {
        trim_each_side = (window - 1) / 2;
    }
    tma_window_ = window;
    tma_trim_   = trim_each_side;
    tma_index_ = 0;
    tma_count_ = 0;
    for (uint8_t i = 0; i < TMA_MAX_WINDOW; ++i) tma_buffer_[i] = 0.0f;
}

float hx711::read_weight_trimmed_mavg()
{
    float x = read_weight(1);
    tma_buffer_[tma_index_] = x;
    tma_index_ = (tma_index_ + 1) % tma_window_;
    if (tma_count_ < tma_window_) tma_count_++;

    if (tma_count_ < tma_window_) {
        float sum = 0.0f;
        for (uint8_t i = 0; i < tma_count_; ++i) sum += tma_buffer_[i];
        return sum / tma_count_;
    }

    float tmp[TMA_MAX_WINDOW];
    for (uint8_t i = 0; i < tma_window_; ++i) tmp[i] = tma_buffer_[i];
    std::sort(tmp, tmp + tma_window_);

    uint8_t start = tma_trim_;
    uint8_t end = tma_window_ - tma_trim_;
    float sum = 0.0f;
    for (uint8_t i = start; i < end; ++i) sum += tmp[i];
    return sum / (end - start);
}

///////////////////////////////////////////////////////
//
//  TARE
//
void hx711::tare(int samples)
{
    if (samples < 1) samples = 7;
    offset_ = (int32_t)calibr_read_average((uint8_t)samples);
    printf("Tare Offset : %d\n",offset_);
}

// Return the tare offset expressed in grams
float hx711::get_tare()
{
    // offset = counts, scale = counts/gram
    // grams = offset / counts_per_gram
    return -(float)offset_ / scale_cpg_;
}

bool hx711::tare_set()
{
    return offset_ != 0;
}

///////////////////////////////////////////////////////////////
//
//  CALIBRATION  (tare see above)
//
void hx711::set_scale(float counts_per_gram)
{
    // guard against zero / nonsense
    if (counts_per_gram <= 0.0f)
        counts_per_gram = 1.0f;
    scale_cpg_ = counts_per_gram;
}

float hx711::get_scale() const
{
    return scale_cpg_;
}

void hx711::set_offset(int32_t off)
{
    offset_ = off;
}

int32_t hx711::get_offset() const
{
    return offset_;
}

// Assumes tare() has been set.
// Use calibration averaging & discard ONLY here
void hx711::calibrate_scale(float known_grams, int samples /*=10*/)
{
    if (samples < 1) samples = 7;
    if (known_grams <= 0.0f) return;   // ignore bad input
    printf("Known gramms : %6.f\n",known_grams);
    // Assumes you've already called tare()
    int32_t avg = (int32_t)calibr_read_average((uint8_t)samples);
    int32_t net = avg - offset_;                // counts due to known_grams
    float cpg = (float)net / known_grams;       // counts per gram
    if (cpg <= 0.0f) cpg = 1.0f;
    printf("cpg : %6f\n", cpg);
    printf("offset : %d\n", offset_);
    scale_cpg_ = cpg;
}

///////////////////////////////////////////////////////////////
//
//  POWER MANAGEMENT
//
void hx711::power_down()
{
    // stop state machine so it won't fight us for the pin
    pio_sm_set_enabled(pio_, sm_, false);

    gpio_put(clockPin_, 1); // drive PD_SCK high
    sleep_us(64);           // ≥60 µs

    // leave it high; hx711 stays in power-down
}

void hx711::power_up()
{
    gpio_put(clockPin_, 0); // pull PD_SCK low
    sleep_us(1);            // small settle
    // re-enable the state machine when you want to read again
    pio_sm_set_enabled(pio_, sm_, true);
}
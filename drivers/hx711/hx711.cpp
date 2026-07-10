// hx711.cpp - hx711 driver for Raspberry Pi Pico
// Copyright (c) 2025 Max Penfold
// MIT License
//
// Portions derived from hx711 Arduino library
// Copyright (c) 2019-2025 Rob Tillaart
// MIT License

#include "hx711.hpp"
#include <cstdio>
#include <cmath>
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

// Wait up to timeout_us for a conversion. A disconnected/unpowered HX711 never
// pushes a sample - without a timeout, the first read would block the whole
// firmware forever (frozen LCD, dead network).
bool hx711::read_raw_timeout(int32_t& out, uint32_t timeout_us)
{
    absolute_time_t deadline = make_timeout_time_us(timeout_us);
    while (pio_sm_is_rx_fifo_empty(pio_, sm_))
    {
        if (time_reached(deadline)) return false;
        tight_loop_contents();
    }
    uint32_t raw = pio_sm_get(pio_, sm_);
    if (raw & 0x00800000) raw |= 0xFF000000;
    out = static_cast<int32_t>(raw);
    return true;
}

// FIFO discard before reading average. Returns NAN if the sensor produces no
// data (disconnected) so callers can abort instead of freezing the firmware.
float hx711::calibr_read_average(uint8_t times)
{
    const uint32_t discardReads = 6;
    int64_t sum = 0;                 // accumulate in wider int
    for (uint8_t i = 0; i < times + discardReads; i++)
    {
        int32_t v;
        if (!read_raw_timeout(v, 150000)) {
            printf("[hx711] calibr read timeout on DT pin %u\n", dataPin_);
            return NAN;
        }
        if (i >= discardReads) sum += v;
    }
    float calibration = (float)sum / (float)times;
    printf("Calibration read average : %.6f\n", calibration);
    return calibration;
}

///////////////////////////////////////////////////////
//
//  MODE
//
// Drain the RX FIFO, keeping only the newest queued sample. The HX711 free-runs
// at ~10 SPS into a 4-deep FIFO; when consumed slower than that, the oldest
// entry is ~1 s stale - always skip to the freshest.
bool hx711::drain_fifo(int32_t& newest)
{
    bool got = false;
    while (!pio_sm_is_rx_fifo_empty(pio_, sm_))
    {
        uint32_t raw = pio_sm_get(pio_, sm_);
        if (raw & 0x00800000) raw |= 0xFF000000;
        newest = (int32_t)raw;
        got = true;
    }
    return got;
}

float hx711::read_weight(int samples /*=1*/)
{
    if (samples < 1) samples = 1;

    // ~150 ms covers one conversion at 10 SPS with margin
    constexpr uint32_t SAMPLE_TIMEOUT_US = 150000;

    if (samples == 1)
    {
        // Freshest-available, non-blocking once a first sample exists:
        // take the newest queued sample, else keep the last known one.
        int32_t v;
        if (drain_fifo(v)) {
            last_raw_ = v;
            has_last_ = true;
        } else if (!has_last_) {
            // Very first read: wait one conversion, but never hang on a dead
            // sensor - probe again at most every 2 s and report 0 g meanwhile
            if (!time_reached(next_probe_)) return 0.0f;
            if (read_raw_timeout(v, SAMPLE_TIMEOUT_US)) {
                last_raw_ = v;
                has_last_ = true;
            } else {
                printf("[hx711] no data on DT pin %u (sensor disconnected?)\n", dataPin_);
                next_probe_ = make_timeout_time_ms(2000);
                return 0.0f;
            }
        }
    }
    else
    {
        // Averaged read: discard the stale backlog, then wait for fresh samples
        int32_t dump;
        drain_fifo(dump);
        int64_t sum = 0;
        int got = 0;
        for (int i = 0; i < samples; ++i) {
            int32_t v;
            if (!read_raw_timeout(v, SAMPLE_TIMEOUT_US)) break;
            sum += (int64_t)v;
            got++;
        }
        if (got == 0) {
            if (!has_last_) return 0.0f;   // dead sensor - keep last known if any
        } else {
            last_raw_ = (int32_t)(sum / got);
            has_last_ = true;
        }
    }

    int32_t net = last_raw_ - offset_;
    return (float)net / scale_cpg_;    // counts-per-gram
}

float hx711::last_gross() const
{
    return (float)(last_raw_ - cal_offset_) / scale_cpg_;
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
    float avg = calibr_read_average((uint8_t)samples);
    if (avg != avg) {   // NAN - dead sensor, keep the previous offset
        printf("Tare skipped: no sensor data\n");
        return;
    }
    offset_ = (int32_t)avg;
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

void hx711::set_cal_offset(int32_t off)
{
    cal_offset_ = off;
}

int32_t hx711::get_cal_offset() const
{
    return cal_offset_;
}

// Assumes tare() has been set.
// Use calibration averaging & discard ONLY here
void hx711::calibrate_scale(float known_grams, int samples /*=10*/)
{
    if (samples < 1) samples = 7;
    if (known_grams <= 0.0f) return;   // ignore bad input
    printf("Known gramms : %6.f\n",known_grams);
    // Assumes you've already called tare()
    float avg_f = calibr_read_average((uint8_t)samples);
    if (avg_f != avg_f) {   // NAN - dead sensor, leave calibration unchanged
        printf("Calibrate skipped: no sensor data\n");
        return;
    }
    int32_t avg = (int32_t)avg_f;
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
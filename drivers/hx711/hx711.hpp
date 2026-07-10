#pragma once

#include <cstdint>
#include "hardware/pio.h"
#include "pico/time.h"

class hx711
{
public:
    hx711(uint clockPin, uint dataPin);

    ///////////////////////////////////////////////////////////////
    //
    //  READ
    //
    //  From datasheet page 4
    int32_t read_raw_hx711();
    //  Timeout-guarded read: false if no conversion arrived (dead sensor).
    //  A disconnected HX711 must never freeze the firmware.
    bool    read_raw_timeout(int32_t& out, uint32_t timeout_us);
    //  Returns NAN when the sensor produces no data.
    float   calibr_read_average(uint8_t times);

    ///////////////////////////////////////////////////////
    //
    //  MODE
    //
    //  read_weight(1) is non-blocking after the first call: it drains the PIO RX
    //  FIFO and uses the NEWEST queued sample (the HX711 free-runs at ~10 SPS and
    //  the 4-deep FIFO otherwise serves ~1 s old readings). samples>1 discards the
    //  backlog, then blocks for that many fresh conversions.
    float  read_weight(int samples = 1);
    //  Gross (absolute) weight from the same sample read_weight last used, computed
    //  against the CALIBRATED zero - unaffected by tare(). E.g. the corn bag's true
    //  weight while dispense logic works tare-relative.
    float  last_gross() const;
    void  set_trimmed_mavg_params(uint8_t window, uint8_t trim_each_side);
    float read_weight_trimmed_mavg();

    ///////////////////////////////////////////////////////
    //
    //  TARE
    //
    void    tare(int samples = 10);
    float   get_tare();
    bool    tare_set();

    ///////////////////////////////////////////////////////////////
    //
    //  CALIBRATION  (tare see above)
    //
    void    set_scale(float counts_per_gram);
    float   get_scale() const;
    void    set_offset(int32_t offset);
    int32_t get_offset() const;

    //  Calibrated zero point (raw counts with the scale empty). Persisted in flash
    //  and, unlike offset_, NEVER overwritten by tare() - the basis for last_gross().
    void    set_cal_offset(int32_t offset);
    int32_t get_cal_offset() const;

    //  clear the scale
    //  call tare() to set the zero offset
    //  put a known weight on the scale
    //  call calibrate_scale(weight)
    //  scale is calculated.
    //  Calibrate: known weight in grams, compute counts_per_gram
    void   calibrate_scale(float known_grams, int samples = 10);

    ///////////////////////////////////////////////////////////////
    //
    //  POWER MANAGEMENT
    //
    void    power_down();
    void    power_up();

private:
    PIO pio_ = pio0;
    uint sm_;

    static inline bool programLoaded = false;
    static inline uint programOffset = 0;

    // Drain the RX FIFO keeping only the newest queued sample; true if any was queued
    bool drain_fifo(int32_t& newest);

    uint    clockPin_;
    uint    dataPin_;
    float   scale_cpg_ { 1.0f }; // counts per gram
    int32_t offset_    { 0 };    // tare zero (runtime, clobbered by tare())
    int32_t cal_offset_{ 0 };    // calibrated zero (from flash, survives tare)
    int32_t last_raw_  { 0 };    // newest raw sample seen by read_weight
    bool    has_last_  { false };
    absolute_time_t next_probe_ { 0 };  // backoff for probing a silent sensor

    // --- Trimmed moving average state ---
    static constexpr uint8_t TMA_MAX_WINDOW = 16; // supports up to 16 samples
    uint8_t  tma_window_ { 10 };                  // default: 10-sample window
    uint8_t  tma_trim_   { 2 };                   // default: drop 2 on each side
    uint8_t  tma_index_  { 0 };                   // ring buffer index
    uint8_t  tma_count_  { 0 };                   // number of valid samples in buffer
    float    tma_buffer_[TMA_MAX_WINDOW] { 0.0f }; // ring buffer storage
};
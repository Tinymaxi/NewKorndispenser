#pragma once

#include <cstdint>
#include "hardware/pio.h"

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
    float   calibr_read_average(uint8_t times);

    ///////////////////////////////////////////////////////
    //
    //  MODE
    //
    float  read_weight(int samples = 1);
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

    uint    clockPin_;
    uint    dataPin_;
    float   scale_cpg_ { 1.0f }; // counts per gram
    int32_t offset_    { 0 };

    // --- Trimmed moving average state ---
    static constexpr uint8_t TMA_MAX_WINDOW = 16; // supports up to 16 samples
    uint8_t  tma_window_ { 10 };                  // default: 10-sample window
    uint8_t  tma_trim_   { 2 };                   // default: drop 2 on each side
    uint8_t  tma_index_  { 0 };                   // ring buffer index
    uint8_t  tma_count_  { 0 };                   // number of valid samples in buffer
    float    tma_buffer_[TMA_MAX_WINDOW] { 0.0f }; // ring buffer storage
};
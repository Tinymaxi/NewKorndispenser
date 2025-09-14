#include "config_store.hpp"

#include <cstddef>
#include <cstring>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h" 

#ifndef CFG_SECTOR_SIZE
#define CFG_SECTOR_SIZE   FLASH_SECTOR_SIZE   // 4096
#endif
#ifndef CFG_PAGE_SIZE
#define CFG_PAGE_SIZE     FLASH_PAGE_SIZE     // 256
#endif

static constexpr uint32_t CFG_OFFSET   = (PICO_FLASH_SIZE_BYTES - CFG_SECTOR_SIZE);
static constexpr uint32_t CFG_XIP_ADDR = (XIP_BASE + CFG_OFFSET);

// CRC32 (poly 0xEDB88320)
static uint32_t crc32_calc(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        c ^= p[i];
        for (int b = 0; b < 8; ++b) {
            c = (c >> 1) ^ (0xEDB88320u & (-(int)(c & 1)));
        }
    }
    return ~c;
}

bool load_scale_config(ScaleConfig& cfg) {
    ScaleConfig tmp{};
    std::memcpy(&tmp, reinterpret_cast<const void*>(CFG_XIP_ADDR), sizeof(tmp));

    if (tmp.magic != 0x48583133) return false;

    uint32_t expected = crc32_calc(&tmp, offsetof(ScaleConfig, crc32));
    if (expected != tmp.crc32) return false;

    cfg = tmp;
    return true;
}

bool save_scale_config(const ScaleConfig& cfg_in) {
    alignas(FLASH_PAGE_SIZE) static uint8_t sector_buf[CFG_SECTOR_SIZE];
    std::memset(sector_buf, 0xFF, sizeof(sector_buf));

    ScaleConfig tmp = cfg_in;
    tmp.crc32 = crc32_calc(&tmp, offsetof(ScaleConfig, crc32));
    std::memcpy(sector_buf, &tmp, sizeof(tmp));

    uint32_t irq_state = save_and_disable_interrupts();
    flash_range_erase(CFG_OFFSET, CFG_SECTOR_SIZE);
    flash_range_program(CFG_OFFSET, sector_buf, CFG_SECTOR_SIZE);
    restore_interrupts(irq_state);

    ScaleConfig check{};
    std::memcpy(&check, reinterpret_cast<const void*>(CFG_XIP_ADDR), sizeof(check));
    return (check.magic == tmp.magic) && (check.crc32 == tmp.crc32);
}
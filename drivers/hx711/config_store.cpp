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
// ---- PID gain persistence (second-to-last sector; calibration untouched) ----

static constexpr uint32_t PID_CFG_OFFSET   = (PICO_FLASH_SIZE_BYTES - 2 * CFG_SECTOR_SIZE);
static constexpr uint32_t PID_CFG_XIP_ADDR = (XIP_BASE + PID_CFG_OFFSET);

bool load_pid_config(PidConfig& cfg) {
    PidConfig tmp{};
    std::memcpy(&tmp, reinterpret_cast<const void*>(PID_CFG_XIP_ADDR), sizeof(tmp));

    if (tmp.magic != 0x50494431) return false;

    uint32_t expected = crc32_calc(&tmp, offsetof(PidConfig, crc32));
    if (expected != tmp.crc32) return false;

    // Reject garbage that happens to pass CRC-of-garbage odds: gains must be
    // finite and non-negative (NaN fails all comparisons, so use !(x >= 0)).
    if (!(tmp.kp >= 0.0f) || !(tmp.ki >= 0.0f) || !(tmp.kd >= 0.0f)) return false;
    if (tmp.kp > 1000.0f || tmp.ki > 1000.0f || tmp.kd > 1000.0f) return false;

    cfg = tmp;
    return true;
}

bool save_pid_config(const PidConfig& cfg_in) {
    alignas(FLASH_PAGE_SIZE) static uint8_t sector_buf[CFG_SECTOR_SIZE];
    std::memset(sector_buf, 0xFF, sizeof(sector_buf));

    PidConfig tmp = cfg_in;
    tmp.magic = 0x50494431;
    tmp.crc32 = crc32_calc(&tmp, offsetof(PidConfig, crc32));
    std::memcpy(sector_buf, &tmp, sizeof(tmp));

    uint32_t irq_state = save_and_disable_interrupts();
    flash_range_erase(PID_CFG_OFFSET, CFG_SECTOR_SIZE);
    flash_range_program(PID_CFG_OFFSET, sector_buf, CFG_SECTOR_SIZE);
    restore_interrupts(irq_state);

    PidConfig check{};
    std::memcpy(&check, reinterpret_cast<const void*>(PID_CFG_XIP_ADDR), sizeof(check));
    return (check.magic == tmp.magic) && (check.crc32 == tmp.crc32);
}

// ---- Per-scale content names (third-to-last sector) -------------------------

static constexpr uint32_t NAME_CFG_OFFSET   = (PICO_FLASH_SIZE_BYTES - 3 * CFG_SECTOR_SIZE);
static constexpr uint32_t NAME_CFG_XIP_ADDR = (XIP_BASE + NAME_CFG_OFFSET);

bool load_name_config(NameConfig& cfg) {
    NameConfig tmp{};
    std::memcpy(&tmp, reinterpret_cast<const void*>(NAME_CFG_XIP_ADDR), sizeof(tmp));

    if (tmp.magic != 0x4E414D31) return false;

    uint32_t expected = crc32_calc(&tmp, offsetof(NameConfig, crc32));
    if (expected != tmp.crc32) return false;

    // Enforce termination and printable ASCII (the names travel through JSON,
    // CSV metadata and the HD44780 LCD)
    for (int i = 0; i < 3; i++) {
        tmp.names[i][SCALE_NAME_LEN - 1] = '\0';
        for (char* p = tmp.names[i]; *p; ++p) {
            if (*p < 0x20 || *p > 0x7E) { *p = '\0'; break; }
        }
    }

    cfg = tmp;
    return true;
}

bool save_name_config(const NameConfig& cfg_in) {
    alignas(FLASH_PAGE_SIZE) static uint8_t sector_buf[CFG_SECTOR_SIZE];
    std::memset(sector_buf, 0xFF, sizeof(sector_buf));

    NameConfig tmp = cfg_in;
    tmp.magic = 0x4E414D31;
    tmp.crc32 = crc32_calc(&tmp, offsetof(NameConfig, crc32));
    std::memcpy(sector_buf, &tmp, sizeof(tmp));

    uint32_t irq_state = save_and_disable_interrupts();
    flash_range_erase(NAME_CFG_OFFSET, CFG_SECTOR_SIZE);
    flash_range_program(NAME_CFG_OFFSET, sector_buf, CFG_SECTOR_SIZE);
    restore_interrupts(irq_state);

    NameConfig check{};
    std::memcpy(&check, reinterpret_cast<const void*>(NAME_CFG_XIP_ADDR), sizeof(check));
    return (check.magic == tmp.magic) && (check.crc32 == tmp.crc32);
}

// ---- Per-servo zero angles (fourth-to-last sector) --------------------------

static constexpr uint32_t SERVO_CFG_OFFSET   = (PICO_FLASH_SIZE_BYTES - 4 * CFG_SECTOR_SIZE);
static constexpr uint32_t SERVO_CFG_XIP_ADDR = (XIP_BASE + SERVO_CFG_OFFSET);

bool load_servo_config(ServoConfig& cfg) {
    ServoConfig tmp{};
    std::memcpy(&tmp, reinterpret_cast<const void*>(SERVO_CFG_XIP_ADDR), sizeof(tmp));

    if (tmp.magic != 0x53525631) return false;

    uint32_t expected = crc32_calc(&tmp, offsetof(ServoConfig, crc32));
    if (expected != tmp.crc32) return false;

    // Normalize per entry: anything outside 0-180 degrees (incl. NaN, which
    // fails all comparisons) means "not calibrated" for that servo only
    for (int i = 0; i < 3; i++) {
        if (!(tmp.open_deg[i] >= 0.0f && tmp.open_deg[i] <= 180.0f)) {
            tmp.open_deg[i] = -1.0f;
        }
    }

    cfg = tmp;
    return true;
}

bool save_servo_config(const ServoConfig& cfg_in) {
    alignas(FLASH_PAGE_SIZE) static uint8_t sector_buf[CFG_SECTOR_SIZE];
    std::memset(sector_buf, 0xFF, sizeof(sector_buf));

    ServoConfig tmp = cfg_in;
    tmp.magic = 0x53525631;
    tmp.crc32 = crc32_calc(&tmp, offsetof(ServoConfig, crc32));
    std::memcpy(sector_buf, &tmp, sizeof(tmp));

    uint32_t irq_state = save_and_disable_interrupts();
    flash_range_erase(SERVO_CFG_OFFSET, CFG_SECTOR_SIZE);
    flash_range_program(SERVO_CFG_OFFSET, sector_buf, CFG_SECTOR_SIZE);
    restore_interrupts(irq_state);

    ServoConfig check{};
    std::memcpy(&check, reinterpret_cast<const void*>(SERVO_CFG_XIP_ADDR), sizeof(check));
    return (check.magic == tmp.magic) && (check.crc32 == tmp.crc32);
}

// ---- Network boot mode (fifth-to-last sector) --------------------------------

static constexpr uint32_t NET_CFG_OFFSET   = (PICO_FLASH_SIZE_BYTES - 5 * CFG_SECTOR_SIZE);
static constexpr uint32_t NET_CFG_XIP_ADDR = (XIP_BASE + NET_CFG_OFFSET);

bool load_net_config(NetConfig& cfg) {
    NetConfig tmp{};
    std::memcpy(&tmp, reinterpret_cast<const void*>(NET_CFG_XIP_ADDR), sizeof(tmp));

    if (tmp.magic != 0x4E455431) return false;

    uint32_t expected = crc32_calc(&tmp, offsetof(NetConfig, crc32));
    if (expected != tmp.crc32) return false;

    if (tmp.mode > 1) return false;

    cfg = tmp;
    return true;
}

bool save_net_config(const NetConfig& cfg_in) {
    alignas(FLASH_PAGE_SIZE) static uint8_t sector_buf[CFG_SECTOR_SIZE];
    std::memset(sector_buf, 0xFF, sizeof(sector_buf));

    NetConfig tmp = cfg_in;
    tmp.magic = 0x4E455431;
    tmp.crc32 = crc32_calc(&tmp, offsetof(NetConfig, crc32));
    std::memcpy(sector_buf, &tmp, sizeof(tmp));

    uint32_t irq_state = save_and_disable_interrupts();
    flash_range_erase(NET_CFG_OFFSET, CFG_SECTOR_SIZE);
    flash_range_program(NET_CFG_OFFSET, sector_buf, CFG_SECTOR_SIZE);
    restore_interrupts(irq_state);

    NetConfig check{};
    std::memcpy(&check, reinterpret_cast<const void*>(NET_CFG_XIP_ADDR), sizeof(check));
    return (check.magic == tmp.magic) && (check.crc32 == tmp.crc32);
}

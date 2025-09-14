#pragma once
#include <cstdint>
#include <string_view>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

/**
 * Minimal HD44780 driver via PCF8574T I2C backpack (YWROBOT/DFRobot style).
 * Bit mapping = same as Raspberry Pi example:
 *   P0=RS, P1=RW (kept low), P2=EN, P3..P6=D4..D7, P7=Backlight (active high)
 */
class Lcd1602I2C {
public:
    // addr: 0x20..0x27 depending on A2/A1/A0 jumpers
    Lcd1602I2C(i2c_inst_t* i2c, uint8_t addr, uint8_t cols=16, uint8_t rows=2);

    // Initialize I2C pins and LCD. Default Pico I2C0 pins are SDA=4, SCL=5.
    void init(uint sda_gpio, uint scl_gpio, uint32_t baud = 100000);

    // Basic API
    void clear();
    void home();
    void setCursor(uint8_t line, uint8_t col);
    void writeChar(char c);
    void print(const char* s);
    void print(std::string_view s);

    // Display controls
    void displayOn(bool on);
    void cursor(bool on);
    void blink(bool on);
    void backlight(bool on);

    // Helper to find first I2C device on the bus (returns 0xFF if none)
    static uint8_t scanFirst(i2c_inst_t* i2c);

private:
    // Low-level (1-byte write, like the RPi example)
    inline void i2cWriteByte(uint8_t v) {
        i2c_write_blocking(_i2c, _addr, &v, 1, false);
    }

    // Transfers
    void sendCmd(uint8_t cmd);
    void sendData(uint8_t data);
    void sendNibble(uint8_t nibbleUpper, bool rs);
    void pulseEnable(uint8_t busByte);

private:
    i2c_inst_t* _i2c;
    uint8_t _addr;
    uint8_t _cols, _rows;

    // PCF8574 bits (match RPi example)
    static constexpr uint8_t LCD_BACKLIGHT = 0x08; // P7
    static constexpr uint8_t LCD_ENABLE    = 0x04; // P2
    static constexpr uint8_t LCD_RW        = 0x02; // P1 (we keep low)
    static constexpr uint8_t LCD_RS        = 0x01; // P0

    // Commands / flags
    static constexpr uint8_t LCD_CLEARDISPLAY   = 0x01;
    static constexpr uint8_t LCD_RETURNHOME     = 0x02;
    static constexpr uint8_t LCD_ENTRYMODESET   = 0x04;
    static constexpr uint8_t LCD_DISPLAYCONTROL = 0x08;
    static constexpr uint8_t LCD_CURSORSHIFT    = 0x10;
    static constexpr uint8_t LCD_FUNCTIONSET    = 0x20;
    static constexpr uint8_t LCD_SETDDRAMADDR   = 0x80;

    // Entry mode
    static constexpr uint8_t LCD_ENTRYLEFT            = 0x02;
    static constexpr uint8_t LCD_ENTRYSHIFTINCREMENT  = 0x01;

    // Display control
    static constexpr uint8_t LCD_DISPLAYON = 0x04;
    static constexpr uint8_t LCD_CURSORON  = 0x02;
    static constexpr uint8_t LCD_BLINKON   = 0x01;

    // Function set
    static constexpr uint8_t LCD_2LINE     = 0x08;

    uint8_t _displayCtl = LCD_DISPLAYON; // display on, cursor/blink off
    uint8_t _entryMode  = LCD_ENTRYLEFT; // increment, no shift
    uint8_t _blMask     = LCD_BACKLIGHT; // backlight ON
};
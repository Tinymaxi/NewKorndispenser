#include "Lcd1602I2C.hpp"
#include "pico/binary_info.h"

Lcd1602I2C::Lcd1602I2C(i2c_inst_t* i2c, uint8_t addr, uint8_t cols, uint8_t rows)
: _i2c(i2c), _addr(addr), _cols(cols), _rows(rows) {}

void Lcd1602I2C::init(uint sda_gpio, uint scl_gpio, uint32_t baud) {
    // I2C setup
    i2c_init(_i2c, baud);
    gpio_set_function(sda_gpio, GPIO_FUNC_I2C);
    gpio_set_function(scl_gpio, GPIO_FUNC_I2C);
    gpio_pull_up(sda_gpio);
    gpio_pull_up(scl_gpio);
    bi_decl(bi_2pins_with_func(sda_gpio, scl_gpio, GPIO_FUNC_I2C));

    // Power-up wait
    sleep_ms(50);

    // 4-bit init sequence (exactly like the RPi example)
    auto send_init_nibble = [&](uint8_t v) {
        // v is 0x03 or 0x02 in lower nibble; we send it as an upper nibble
        uint8_t high = (v << 4) | _blMask; // RS=0, RW=0
        i2cWriteByte(high);
        pulseEnable(high);
    };
    send_init_nibble(0x03); sleep_ms(5);
    send_init_nibble(0x03); sleep_us(150);
    send_init_nibble(0x03); sleep_us(150);
    send_init_nibble(0x02); // set 4-bit

    // Function set: 4-bit, 2-line (5x8 font)
    sendCmd(LCD_FUNCTIONSET | LCD_2LINE);

    // Display off
    _displayCtl = 0;
    sendCmd(LCD_DISPLAYCONTROL | _displayCtl);

    // Clear
    clear();

    // Entry mode
    _entryMode = LCD_ENTRYLEFT;
    sendCmd(LCD_ENTRYMODESET | _entryMode);

    // Display on
    _displayCtl = LCD_DISPLAYON;
    sendCmd(LCD_DISPLAYCONTROL | _displayCtl);
}

void Lcd1602I2C::clear() { sendCmd(LCD_CLEARDISPLAY); sleep_ms(2); }
void Lcd1602I2C::home()  { sendCmd(LCD_RETURNHOME);   sleep_ms(2); }

void Lcd1602I2C::setCursor(uint8_t line, uint8_t col) {
    // 20x4 row addressing
    static const uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
    if (line >= _rows) line = _rows - 1;
    sendCmd(LCD_SETDDRAMADDR | (row_offsets[line] + col));
}

void Lcd1602I2C::writeChar(char c) { sendData((uint8_t)c); }

void Lcd1602I2C::print(const char* s) {
    if (!s) return;
    while (*s) writeChar(*s++);
}
void Lcd1602I2C::print(std::string_view s) {
    for (char c : s) writeChar(c);
}

void Lcd1602I2C::displayOn(bool on) {
    if (on) _displayCtl |=  LCD_DISPLAYON;
    else    _displayCtl &= ~LCD_DISPLAYON;
    sendCmd(LCD_DISPLAYCONTROL | _displayCtl);
}
void Lcd1602I2C::cursor(bool on) {
    if (on) _displayCtl |=  LCD_CURSORON;
    else    _displayCtl &= ~LCD_CURSORON;
    sendCmd(LCD_DISPLAYCONTROL | _displayCtl);
}
void Lcd1602I2C::blink(bool on) {
    if (on) _displayCtl |=  LCD_BLINKON;
    else    _displayCtl &= ~LCD_BLINKON;
    sendCmd(LCD_DISPLAYCONTROL | _displayCtl);
}
void Lcd1602I2C::backlight(bool on) {
    _blMask = on ? LCD_BACKLIGHT : 0x00;
    // poke bus (no-op) to apply new BL bit: resend display control
    sendCmd(LCD_DISPLAYCONTROL | _displayCtl);
}

// --- transfers (same bus timings as the RPi example) ---
void Lcd1602I2C::sendCmd(uint8_t v) {
    // high then low nibble, RS=0
    sendNibble(v & 0xF0, false);
    sendNibble((uint8_t)((v << 4) & 0xF0), false);
}
void Lcd1602I2C::sendData(uint8_t v){
    // high then low nibble, RS=1
    sendNibble(v & 0xF0, true);
    sendNibble((uint8_t)((v << 4) & 0xF0), true);
}

void Lcd1602I2C::pulseEnable(uint8_t bus) {
    // Toggle EN with ~600 µs delays like the RPi code
    constexpr uint32_t DELAY_US = 600;
    sleep_us(DELAY_US);
    i2cWriteByte(bus | LCD_ENABLE);
    sleep_us(DELAY_US);
    i2cWriteByte(bus & ~LCD_ENABLE);
    sleep_us(DELAY_US);
}

void Lcd1602I2C::sendNibble(uint8_t nibbleUpper, bool rs) {
    // nibbleUpper already in bits 7..4 (xxxx0000)
    // Build byte with BL + RS + upper nibble. RW=0 always.
    uint8_t out = _blMask | (rs ? LCD_RS : 0x00) | (nibbleUpper & 0xF0);
    i2cWriteByte(out);
    pulseEnable(out);
}

uint8_t Lcd1602I2C::scanFirst(i2c_inst_t* i2c) {
    for (uint8_t a = 0x03; a <= 0x77; ++a) {
        uint8_t dummy = 0;
        int r = i2c_write_blocking(i2c, a, &dummy, 0, false);
        if (r >= 0) return a;
    }
    return 0xFF;
}
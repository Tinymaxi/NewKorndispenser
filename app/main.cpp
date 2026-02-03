#include "pico/stdlib.h"
#include <cstdio>

#include "Buzzer.hpp"
#include "Rotary_Button.hpp"
#include "Lcd1602I2C.hpp"
#include "hx711.hpp"
#include "config_store.hpp"
#include "Ws2812.hpp"
#include "SevenSeg.hpp"

constexpr uint BUZZER_PIN = 2;
constexpr uint SEVENSEG_PIN = 28;
constexpr uint SEVENSEG_LEDS = 48;  // 6 digits x 8 segments

Rotary_Button enc;
Buzzer bz(BUZZER_PIN);
Lcd1602I2C lcd(i2c0, 0x27, 20, 4);
hx711 scaleA(16, 17);

// Pointers - initialized in main() to avoid PIO conflict with encoder
Ws2812* sevenSegStrip = nullptr;
SevenSeg* sevenSeg = nullptr;

ScaleConfig sc;

// void indicatorArrow(int lineNumber);

int main()
{
    stdio_init_all();

    while (!stdio_usb_connected())
    {
        sleep_ms(500);
    }

    printf("Serial connected. Ready.\r\r\n");

    bz.beep(); // startup chirp
    lcd.init(PICO_DEFAULT_I2C_SDA_PIN, PICO_DEFAULT_I2C_SCL_PIN, 100000);

    // Initialize 7-segment display after encoder to avoid PIO conflict
    sevenSegStrip = new Ws2812(SEVENSEG_PIN, SEVENSEG_LEDS);
    sevenSeg = new SevenSeg(*sevenSegStrip, LAYOUT_6DIGITS);

    bool isConfigured = false;

    if (load_scale_config(sc))
    {
        // Apply, then verify if the values look like defaults
        apply_scale_config(scaleA, sc.entries[2]);
        const bool looks_default = (scaleA.get_offset() == 0) && (scaleA.get_scale() == 1.0f);

        lcd.clear();
        if (looks_default)
        {
            // Two-line centered message
            lcd.setCursor(0, 0);
            lcd.print("     No valid");
            lcd.setCursor(1, 0);
            lcd.print("  config in flash!   ");

            char line[21]; // 20 cols + null
            lcd.setCursor(2, 0);
            std::snprintf(line, sizeof(line), "Scale: %.6f", scaleA.get_scale());
            lcd.print(line);

            lcd.setCursor(3, 0);
            std::snprintf(line, sizeof(line), "Offset: %ld", (long)scaleA.get_offset());
            lcd.print(line);
            sleep_ms(2500);
        }
        else
        {
            isConfigured = true;
            // Brief confirmation; optionally show values
            lcd.setCursor(0, 0);
            lcd.print("   Loaded config   ");

            char line[21]; // 20 cols + null
            lcd.setCursor(1, 0);
            std::snprintf(line, sizeof(line), "Scale: %.6f", scaleA.get_scale());
            lcd.print(line);

            lcd.setCursor(2, 0);
            std::snprintf(line, sizeof(line), "Offset: %ld", (long)scaleA.get_offset());
            lcd.print(line);
            sleep_ms(1500);
        }
    }
    else
    {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("     No valid");
        lcd.setCursor(1, 0);
        lcd.print("  config in flash   ");
        sleep_ms(1500);
    }

    // lcd.clear();
    // lcd.setCursor(0, 2);
    // lcd.print("Menu");
    // lcd.setCursor(1, 2);
    // lcd.print("Calibrate");
    // lcd.setCursor(2, 2);
    // lcd.print("Weigh");

    enum class ScreenId
    {
        Menu,
        Calibrate1,
        Calibrate2,
        SetTarget,
        Weigh
    };

    // add after enum class ScreenId { ... };
    ScreenId current = isConfigured ? ScreenId::Menu : ScreenId::Calibrate1;
    ScreenId last_screen = static_cast<ScreenId>(-1);
    int last_selected = -1;
    int target_grams = 100;  // default target weight

    // Lambda to draw menu arrow indicator
    auto indicatorArrow = [&](int lineNumber) {
        for (int i = 0; i < 3; i++) {
            lcd.setCursor(i, 0);
            lcd.writeChar(i == lineNumber ? '>' : ' ');
        }
    };

    while (true)
    {
        // render screen header only when screen changes
        if (current != last_screen)
        {
            lcd.clear();
            switch (current)
            {
            case ScreenId::Menu:
                lcd.setCursor(0, 2);
                lcd.print("Calibrate");
                lcd.setCursor(1, 2);
                lcd.print("Set Target");
                lcd.setCursor(2, 2);
                lcd.print("Weigh");
                indicatorArrow(0);  // default highlight
                last_selected = 0;
                break;

            case ScreenId::Calibrate1:
                lcd.setCursor(0, 0);
                lcd.print("Calibrate step 1");
                lcd.setCursor(1, 0);
                lcd.print("Remove all weight");
                lcd.setCursor(2, 0);
                lcd.print("Press button...");
                break;

            case ScreenId::Calibrate2:
                lcd.setCursor(0, 0);
                lcd.print("Calibrate step 2");
                lcd.setCursor(1, 0);
                lcd.print("Place known weight");
                lcd.setCursor(2, 0);
                lcd.print("Enter grams...");
                break;

            case ScreenId::SetTarget:
                lcd.setCursor(0, 0);
                lcd.print("Set Target Weight");
                lcd.setCursor(1, 0);
                lcd.print("Use encoder to adjust");
                lcd.setCursor(2, 0);
                lcd.print("Press to confirm");
                break;

            case ScreenId::Weigh:
                lcd.setCursor(0, 0);
                lcd.print("Weighing...");
                break;
            }
            last_screen = current;
        }

        switch (current)
        {
        case ScreenId::Menu:
        {
            // robust mod for negatives
            int pos = enc.getPosition();
            int selected = ((pos % 3) + 3) % 3;

            if (selected != last_selected)
            {
                indicatorArrow(selected);
                last_selected = selected;
            }

            // example navigation on button press
            if (enc.isPressed())
            {
                if (selected == 0)
                    current = ScreenId::Calibrate1; // go to calibration
                else if (selected == 1)
                    current = ScreenId::SetTarget;  // go to set target
                else
                    current = ScreenId::Weigh;      // go to weighing
            }
            break;
        }

        case ScreenId::Calibrate1:
        {
            if (enc.isPressed())
            {
                scaleA.tare();  // zero the scale with no weight
                current = ScreenId::Calibrate2;
            }
            sleep_ms(50);
            break;
        }

        case ScreenId::Calibrate2:
        {
            // Digit entry: 4 digits + OK button
            // Navigation mode: encoder moves cursor, button enters edit/confirms
            // Edit mode: encoder changes digit value, button exits edit
            static int digits[4] = {1, 0, 5, 6};  // Default 1056g
            static int cursor_pos = 0;  // 0-3 = digits, 4 = OK
            static int last_encoder_pos = 0;
            static bool first_entry = true;
            static bool was_pressed = false;
            static bool edit_mode = false;

            if (first_entry) {
                last_encoder_pos = enc.getPosition();
                first_entry = false;
                was_pressed = false;
                cursor_pos = 0;
                edit_mode = false;
            }

            bool pressed = enc.isPressed();
            int pos = enc.getPosition();
            int delta = pos - last_encoder_pos;

            if (edit_mode) {
                // Edit mode: encoder changes digit value
                if (delta != 0) {
                    digits[cursor_pos] = ((digits[cursor_pos] + delta) % 10 + 10) % 10;
                    last_encoder_pos = pos;
                }
                // Button exits edit mode
                if (pressed && !was_pressed) {
                    edit_mode = false;
                }
            } else {
                // Navigation mode: encoder moves cursor
                if (delta != 0) {
                    cursor_pos += delta;
                    if (cursor_pos < 0) cursor_pos = 0;
                    if (cursor_pos > 4) cursor_pos = 4;
                    last_encoder_pos = pos;
                }
                // Button enters edit mode or confirms OK
                if (pressed && !was_pressed) {
                    if (cursor_pos < 4) {
                        // Enter edit mode for this digit
                        edit_mode = true;
                    } else {
                        // OK pressed - calibrate
                        int known_grams = digits[0] * 1000 + digits[1] * 100 +
                                          digits[2] * 10 + digits[3];
                        if (known_grams < 1) known_grams = 1;

                        scaleA.calibrate_scale((float)known_grams, 10);

                        // Save to flash
                        sc.entries[2].offset_counts = scaleA.get_offset();
                        sc.entries[2].count_per_g = scaleA.get_scale();
                        save_scale_config(sc);

                        first_entry = true;
                        current = ScreenId::Menu;
                    }
                }
            }
            was_pressed = pressed;

            // Display with edit indicator: brackets around digit being edited
            char line1[21];
            if (edit_mode) {
                // Show brackets around the digit being edited
                char d[4][4];
                for (int i = 0; i < 4; i++) {
                    if (i == cursor_pos) {
                        std::snprintf(d[i], 4, "[%d]", digits[i]);
                    } else {
                        std::snprintf(d[i], 4, " %d ", digits[i]);
                    }
                }
                std::snprintf(line1, sizeof(line1), "%s%s%s%s OK",
                             d[0], d[1], d[2], d[3]);
            } else {
                std::snprintf(line1, sizeof(line1), " %d  %d  %d  %d %s",
                             digits[0], digits[1], digits[2], digits[3],
                             cursor_pos == 4 ? "[OK]" : " OK ");
            }
            lcd.setCursor(2, 0);
            lcd.print(line1);

            // Cursor line (only in navigation mode)
            // Display format: " 1  0  5  6  OK " positions 1,4,7,10,13
            char line2[21] = "                    ";
            if (!edit_mode) {
                int cursor_x = 1 + cursor_pos * 3;  // Digits at 1,4,7,10
                if (cursor_pos == 4) cursor_x = 13;  // Under OK
                line2[cursor_x] = '^';
            }
            lcd.setCursor(3, 0);
            lcd.print(line2);

            sleep_ms(50);
            break;
        }

        case ScreenId::SetTarget:
        {
            static int last_encoder_pos_target = 0;
            static bool first_entry_target = true;

            if (first_entry_target) {
                last_encoder_pos_target = enc.getPosition();
                first_entry_target = false;
            }

            // Adjust target with encoder
            int pos = enc.getPosition();
            int delta = pos - last_encoder_pos_target;
            if (delta != 0) {
                target_grams += delta;
                if (target_grams < 1) target_grams = 1;
                if (target_grams > 9999) target_grams = 9999;
                last_encoder_pos_target = pos;
            }

            // Show target on 7-segment display
            sevenSeg->clear();
            sevenSeg->printNumber(target_grams, 0, 255, 0);  // green
            sevenSeg->show();

            // Show on LCD too
            char line[21];
            std::snprintf(line, sizeof(line), "Target: %d g    ", target_grams);
            lcd.setCursor(3, 0);
            lcd.print(line);

            // Button press confirms and goes to Weigh
            if (enc.isPressed()) {
                first_entry_target = true;
                current = ScreenId::Weigh;
            }

            sleep_ms(50);
            break;
        }

        case ScreenId::Weigh:
        {
            // Read weight from scale
            float grams = scaleA.read_weight();
            int display_grams = (int)(grams + 0.5f);  // round to nearest gram

            // Display on 7-segment (color based on target)
            sevenSeg->clear();
            if (display_grams < target_grams - 5) {
                // Under target: blue
                sevenSeg->printNumber(display_grams, 0, 0, 255);
            } else if (display_grams > target_grams + 5) {
                // Over target: red
                sevenSeg->printNumber(display_grams, 255, 0, 0);
            } else {
                // At target: green
                sevenSeg->printNumber(display_grams, 0, 255, 0);
            }
            sevenSeg->show();

            // Also show on LCD
            char line[21];
            std::snprintf(line, sizeof(line), "Weight: %d g    ", display_grams);
            lcd.setCursor(1, 0);
            lcd.print(line);
            std::snprintf(line, sizeof(line), "Target: %d g    ", target_grams);
            lcd.setCursor(2, 0);
            lcd.print(line);

            // Button press returns to menu
            if (enc.isPressed()) {
                current = ScreenId::Menu;
            }

            sleep_ms(20);  // faster update for weighing
            break;
        }
        }
    }
}
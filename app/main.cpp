#include "pico/stdlib.h"
#include <cstdio>

#include "Buzzer.hpp"
#include "Rotary_Button.hpp"
#include "Lcd1602I2C.hpp"
#include "hx711.hpp"
#include "config_store.hpp"

constexpr uint BUZZER_PIN = 2;
Rotary_Button enc;
Buzzer bz(BUZZER_PIN);
Lcd1602I2C lcd(i2c0, 0x27, 20, 4);
hx711 scaleA(16, 17);

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
        Weigh
    };

    // add after enum class ScreenId { ... };
    ScreenId current = isConfigured ? ScreenId::Menu : ScreenId::Calibrate1;
    ScreenId last_screen = static_cast<ScreenId>(-1);
    int last_selected = -1;

    //     while (true)
    //     {

    //         switch (current)
    //         {
    //         case ScreenId::Menu:
    //         {
    //             int pos = enc.getPosition() + 999;
    //             int selected = pos % 3;

    //             if (selected != last_selected)
    //             {
    //                 indicatorArrow(selected);
    //                 printf("Selected %d", pos);
    //                 last_selected = selected;
    //             }
    //             // printf("Screen: Menu\n");

    //             break;
    //         }
    //         case ScreenId::Calibrate1:
    //         {
    //             lcd.setCursor(0, 2);
    //             lcd.print("Menu");
    //             lcd.setCursor(1, 2);
    //             lcd.print("Calibrate");
    //             lcd.setCursor(2, 2);
    //             lcd.print("Weigh");
    //         }
    //         }
    //     }
    // }
    sleep_ms(1500);
    printf("Hello there!");
    while (true)
    {
        int raw = gpio_get(15);
        printf("BTN raw=%d\r\n", raw);
        sleep_ms(100);
        // render screen header only when screen changes
        if (current != last_screen)
        {
            lcd.clear();
            switch (current)
            {
            case ScreenId::Menu:
                lcd.setCursor(0, 2);
                lcd.print("Menu");
                lcd.setCursor(1, 2);
                lcd.print("Calibrate");
                lcd.setCursor(2, 2);
                lcd.print("Weigh");
                // indicatorArrow(0); // default highlight
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
            printf("In Menu!\n");
            // robust mod for negatives
            int pos = enc.getPosition();
            int selected = ((pos % 3) + 3) % 3;

            if (selected != last_selected)
            {
                // indicatorArrow(selected);
                last_selected = selected;
            }

            // example navigation on button press
            if (enc.isPressed())
            {
                if (selected == 0)
                    current = ScreenId::Menu; // no-op / could open submenu
                else if (selected == 1)
                    current = ScreenId::Calibrate1; // go to calibration
                else
                    current = ScreenId::Weigh; // go to weighing
            }
            break;
        }

        case ScreenId::Calibrate1:
        {
            printf("In Calibrate1!\r\n");
            static bool last = false;
            bool pressed = enc.isPressed(); // one call per loop

            if (pressed && !last)
            { // rising edge
                int32_t offset = scaleA.calibr_read_average(20);
                scaleA.set_offset(offset);
                printf("Pressed branch taken. Offset=%ld\r\n", (long)offset);
                // current = ScreenId::Calibrate2;
            }
            last = pressed;
            break;
        }

        case ScreenId::Calibrate2:
        {
            printf("In Calibrate2!\n");
            // Here you'd read an entered gram value and compute scale
            // After computing:
            // scaleA.set_scale(counts_per_gram); // or your API
            // Optionally persist:
            // sc.entries[2].offset_counts = scaleA.get_offset();
            // sc.entries[2].g_per_count   = 1.0f / scaleA.get_scale(); // or whatever your convention is
            // save_scale_config(sc);

            // When done, go to weigh or menu
            // current = ScreenId::Weigh;
            break;
        }

        case ScreenId::Weigh:
        {
            printf("In Weigh!\n");
            int32_t raw = scaleA.read_weight();
            float grams = scaleA.read_raw_hx711(); // or compute using your scale/offset
            char line[21];
            std::snprintf(line, sizeof(line), "g: %.2f   raw:%ld", grams, (long)raw);
            lcd.setCursor(1, 0);
            lcd.print("                ");
            lcd.setCursor(1, 0);
            lcd.print(line);
            break;
        }
        }
    }

    // void indicatorArrow(int lineNumber)
    // {
    //     for (int i = 0; i <= 3; i++)
    //     {
    //         if (lineNumber == i)
    //         {
    //             lcd.setCursor(i, 0);
    //             lcd.writeChar('>');
    //         }
    //         else
    //         {
    //             lcd.setCursor(i, 0);
    //             lcd.writeChar(' ');
    //         }
    //     }
    // }
}
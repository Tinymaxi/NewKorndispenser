#include "pico/stdlib.h"
#include <cstdio>

#include "Buzzer.hpp"
#include "Rotary_Button.hpp"
#include "Lcd1602I2C.hpp"
#include "hx711.hpp"
#include "config_store.hpp"
#include "Ws2812.hpp"
#include "SevenSeg.hpp"
#include "Servo.hpp"
#include "Vibrator.hpp"
#include "PID.hpp"

constexpr uint BUZZER_PIN = 2;
constexpr uint SEVENSEG_PIN = 28;
constexpr uint SEVENSEG_LEDS = 48;  // 6 digits x 8 segments

Rotary_Button enc;
Buzzer bz(BUZZER_PIN);
Lcd1602I2C lcd(i2c0, 0x27, 20, 4);

// Three load cell scales (from schematic)
hx711 scale1(20, 21);  // HX711_1: GP20=SCK, GP21=DT
hx711 scale2(18, 19);  // HX711_2: GP18=SCK, GP19=DT
hx711 scale3(16, 17);  // HX711_3: GP16=SCK, GP17=DT

// Array for easy access by index
hx711* scales[3] = {&scale1, &scale2, &scale3};

// Dispenser actuators - one per scale
Servo servo1(7);      // Scale 1 servo - GPIO 7
Servo servo2(8);      // Scale 2 servo - GPIO 8
Servo servo3(9);      // Scale 3 servo - GPIO 9
Servo* servos[3] = {&servo1, &servo2, &servo3};

Vibrator vib1(27);    // Scale 1 vibrator - GPIO 27
Vibrator vib2(26);    // Scale 2 vibrator - GPIO 26
Vibrator vib3(22);    // Scale 3 vibrator - GPIO 22
Vibrator* vibrators[3] = {&vib1, &vib2, &vib3};

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
        // Apply config to all 3 scales
        apply_scale_config(scale1, sc.entries[0]);
        apply_scale_config(scale2, sc.entries[1]);
        apply_scale_config(scale3, sc.entries[2]);

        // Check if at least one scale is configured
        int configured_count = 0;
        for (int i = 0; i < 3; i++) {
            if (scales[i]->get_offset() != 0 || scales[i]->get_scale() != 1.0f) {
                configured_count++;
            }
        }

        lcd.clear();
        if (configured_count == 0)
        {
            lcd.setCursor(0, 0);
            lcd.print("     No valid");
            lcd.setCursor(1, 0);
            lcd.print("  config in flash!   ");
            lcd.setCursor(2, 0);
            lcd.print("Please calibrate");
            sleep_ms(2000);
        }
        else
        {
            isConfigured = true;
            lcd.setCursor(0, 0);
            lcd.print("   Loaded config   ");
            char line[21];
            std::snprintf(line, sizeof(line), "%d of 3 scales ready", configured_count);
            lcd.setCursor(1, 0);
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
        SelectScale,
        Calibrate1,
        Calibrate2,
        SetTarget,
        SetTargetDigit,
        Weigh,
        Dispense,
        TestVibrator
    };

    ScreenId current = isConfigured ? ScreenId::Menu : ScreenId::SelectScale;
    ScreenId last_screen = static_cast<ScreenId>(-1);
    ScreenId after_select = ScreenId::Calibrate1;  // Where to go after SelectScale
    ScreenId after_target = ScreenId::Weigh;       // Where to go after SetTargetDigit
    int last_selected = -1;
    int target_grams = 100;  // default target weight
    int selected_scale = 0;  // 0=Scale1, 1=Scale2, 2=Scale3

    // Lambda to draw menu arrow indicator (supports up to 4 rows)
    auto indicatorArrow = [&](int lineNumber, int startRow = 0, int count = 3) {
        for (int i = 0; i < count; i++) {
            lcd.setCursor(startRow + i, 0);
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
                lcd.print("Dispense");
                lcd.setCursor(2, 2);
                lcd.print("Weigh");
                lcd.setCursor(3, 2);
                lcd.print("Test");
                indicatorArrow(0, 0, 4);
                last_selected = 0;
                break;

            case ScreenId::SelectScale:
                lcd.setCursor(0, 0);
                lcd.print("Select Scale:");
                lcd.setCursor(1, 2);
                lcd.print("Scale 1");
                lcd.setCursor(2, 2);
                lcd.print("Scale 2");
                lcd.setCursor(3, 2);
                lcd.print("Scale 3");
                indicatorArrow(0, 1, 3);  // Arrow on rows 1-3
                last_selected = 0;
                break;

            case ScreenId::Calibrate1:
            {
                char title[21];
                std::snprintf(title, sizeof(title), "Calibrate Scale %d", selected_scale + 1);
                lcd.setCursor(0, 0);
                lcd.print(title);
                lcd.setCursor(1, 0);
                lcd.print("Remove all weight");
                lcd.setCursor(2, 0);
                lcd.print("Press button...");
                break;
            }

            case ScreenId::Calibrate2:
            {
                char title[21];
                std::snprintf(title, sizeof(title), "Scale %d - Step 2", selected_scale + 1);
                lcd.setCursor(0, 0);
                lcd.print(title);
                lcd.setCursor(1, 0);
                lcd.print("Place known weight");
                break;
            }

            case ScreenId::SetTarget:
                lcd.setCursor(0, 0);
                lcd.print("Set Target Weight");
                lcd.setCursor(1, 0);
                lcd.print("Use encoder to adjust");
                lcd.setCursor(2, 0);
                lcd.print("Press to confirm");
                break;

            case ScreenId::SetTargetDigit:
                lcd.setCursor(0, 0);
                lcd.print("Set Target Weight");
                lcd.setCursor(1, 0);
                lcd.print("Enter grams:");
                break;

            case ScreenId::Weigh:
            {
                char title[21];
                std::snprintf(title, sizeof(title), "Weighing - Scale %d", selected_scale + 1);
                lcd.setCursor(0, 0);
                lcd.print(title);
                break;
            }

            case ScreenId::Dispense:
            {
                char title[21];
                std::snprintf(title, sizeof(title), "Dispense - Scale %d", selected_scale + 1);
                lcd.setCursor(0, 0);
                lcd.print(title);
                break;
            }

            case ScreenId::TestVibrator:
                lcd.setCursor(0, 0);
                lcd.print("Test Vibrators");
                lcd.setCursor(1, 0);
                lcd.print("Select: 1  2  3");
                break;
            }
            last_screen = current;
        }

        switch (current)
        {
        case ScreenId::Menu:
        {
            // robust mod for negatives (4 menu items now)
            int pos = enc.getPosition();
            int selected = ((pos % 4) + 4) % 4;

            if (selected != last_selected)
            {
                indicatorArrow(selected, 0, 4);
                last_selected = selected;
            }

            // Navigation on button press
            if (enc.isPressed())
            {
                if (selected == 0) {
                    after_select = ScreenId::Calibrate1;
                    current = ScreenId::SelectScale;
                } else if (selected == 1) {
                    after_select = ScreenId::Dispense;
                    current = ScreenId::SelectScale;
                } else if (selected == 2) {
                    after_select = ScreenId::Weigh;
                    current = ScreenId::SelectScale;
                } else {
                    // Test - go directly to vibrator test
                    current = ScreenId::TestVibrator;
                }
            }
            break;
        }

        case ScreenId::SelectScale:
        {
            int pos = enc.getPosition();
            int selected = ((pos % 3) + 3) % 3;

            if (selected != last_selected)
            {
                indicatorArrow(selected, 1, 3);  // Arrow on rows 1-3
                last_selected = selected;
            }

            if (enc.isPressed())
            {
                selected_scale = selected;  // 0, 1, or 2
                current = after_select;     // Go to Calibrate1 or Weigh
            }
            break;
        }

        case ScreenId::Calibrate1:
        {
            if (enc.isPressed())
            {
                scales[selected_scale]->tare();  // zero the selected scale
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
                        // OK pressed - calibrate selected scale
                        int known_grams = digits[0] * 1000 + digits[1] * 100 +
                                          digits[2] * 10 + digits[3];
                        if (known_grams < 1) known_grams = 1;

                        scales[selected_scale]->calibrate_scale((float)known_grams, 10);

                        // Save to flash (entry index matches scale index)
                        sc.entries[selected_scale].offset_counts = scales[selected_scale]->get_offset();
                        sc.entries[selected_scale].count_per_g = scales[selected_scale]->get_scale();
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

        case ScreenId::SetTargetDigit:
        {
            // Digit entry for target weight (same UI as calibration)
            static int target_digits[4] = {0, 1, 0, 0};  // Default 100g
            static int target_cursor_pos = 0;  // 0-3 = digits, 4 = OK
            static int last_encoder_pos_td = 0;
            static bool first_entry_td = true;
            static bool was_pressed_td = false;
            static bool edit_mode_td = false;

            if (first_entry_td) {
                // Initialize digits from current target_grams
                target_digits[0] = (target_grams / 1000) % 10;
                target_digits[1] = (target_grams / 100) % 10;
                target_digits[2] = (target_grams / 10) % 10;
                target_digits[3] = target_grams % 10;
                last_encoder_pos_td = enc.getPosition();
                first_entry_td = false;
                was_pressed_td = false;
                target_cursor_pos = 0;
                edit_mode_td = false;
            }

            bool pressed = enc.isPressed();
            int pos = enc.getPosition();
            int delta = pos - last_encoder_pos_td;

            if (edit_mode_td) {
                // Edit mode: encoder changes digit value
                if (delta != 0) {
                    target_digits[target_cursor_pos] = ((target_digits[target_cursor_pos] + delta) % 10 + 10) % 10;
                    last_encoder_pos_td = pos;
                }
                // Button exits edit mode
                if (pressed && !was_pressed_td) {
                    edit_mode_td = false;
                }
            } else {
                // Navigation mode: encoder moves cursor
                if (delta != 0) {
                    target_cursor_pos += delta;
                    if (target_cursor_pos < 0) target_cursor_pos = 0;
                    if (target_cursor_pos > 4) target_cursor_pos = 4;
                    last_encoder_pos_td = pos;
                }
                // Button enters edit mode or confirms OK
                if (pressed && !was_pressed_td) {
                    if (target_cursor_pos < 4) {
                        // Enter edit mode for this digit
                        edit_mode_td = true;
                    } else {
                        // OK pressed - set target and return to caller
                        target_grams = target_digits[0] * 1000 + target_digits[1] * 100 +
                                      target_digits[2] * 10 + target_digits[3];
                        if (target_grams < 1) target_grams = 1;

                        first_entry_td = true;
                        current = after_target;
                    }
                }
            }
            was_pressed_td = pressed;

            // Display with edit indicator
            char line1[21];
            if (edit_mode_td) {
                char d[4][4];
                for (int i = 0; i < 4; i++) {
                    if (i == target_cursor_pos) {
                        std::snprintf(d[i], 4, "[%d]", target_digits[i]);
                    } else {
                        std::snprintf(d[i], 4, " %d ", target_digits[i]);
                    }
                }
                std::snprintf(line1, sizeof(line1), "%s%s%s%s OK",
                             d[0], d[1], d[2], d[3]);
            } else {
                std::snprintf(line1, sizeof(line1), " %d  %d  %d  %d %s",
                             target_digits[0], target_digits[1], target_digits[2], target_digits[3],
                             target_cursor_pos == 4 ? "[OK]" : " OK ");
            }
            lcd.setCursor(2, 0);
            lcd.print(line1);

            // Cursor line (only in navigation mode)
            char line2[21] = "                    ";
            if (!edit_mode_td) {
                int cursor_x = 1 + target_cursor_pos * 3;
                if (target_cursor_pos == 4) cursor_x = 13;
                line2[cursor_x] = '^';
            }
            lcd.setCursor(3, 0);
            lcd.print(line2);

            // Show current value on 7-segment
            int preview = target_digits[0] * 1000 + target_digits[1] * 100 +
                         target_digits[2] * 10 + target_digits[3];
            sevenSeg->clear();
            sevenSeg->printNumber(preview, 0, 255, 0);  // green
            sevenSeg->show();

            sleep_ms(50);
            break;
        }

        case ScreenId::Weigh:
        {
            // State for option selection in Weigh screen
            static int weigh_option = 0;  // 0=Tare, 1=Target, 2=Back
            static int last_weigh_option = -1;
            static int last_encoder_pos_weigh = 0;
            static bool first_entry_weigh = true;
            static bool was_pressed_weigh = false;

            if (first_entry_weigh) {
                last_encoder_pos_weigh = enc.getPosition();
                first_entry_weigh = false;
                was_pressed_weigh = false;
                weigh_option = 0;
                last_weigh_option = -1;
            }

            // Read weight from selected scale
            float grams = scales[selected_scale]->read_weight();
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

            // Handle encoder for option selection
            int pos = enc.getPosition();
            int delta = pos - last_encoder_pos_weigh;
            if (delta != 0) {
                weigh_option += delta;
                if (weigh_option < 0) weigh_option = 0;
                if (weigh_option > 2) weigh_option = 2;
                last_encoder_pos_weigh = pos;
            }

            // Update option display if changed
            if (weigh_option != last_weigh_option) {
                // Format: " Tare   Target   Back"
                // With brackets around selected option
                const char* opts[3] = {"Tare", "Target", "Back"};
                char opt_line[21];
                std::snprintf(opt_line, sizeof(opt_line), "%s%s%s %s%s%s %s%s%s",
                    weigh_option == 0 ? "[" : " ", opts[0], weigh_option == 0 ? "]" : " ",
                    weigh_option == 1 ? "[" : " ", opts[1], weigh_option == 1 ? "]" : " ",
                    weigh_option == 2 ? "[" : " ", opts[2], weigh_option == 2 ? "]" : " ");
                lcd.setCursor(3, 0);
                lcd.print(opt_line);
                last_weigh_option = weigh_option;
            }

            // Handle button press
            bool pressed = enc.isPressed();
            if (pressed && !was_pressed_weigh) {
                if (weigh_option == 0) {
                    // Tare - zero the scale
                    scales[selected_scale]->tare();
                    bz.beep();  // feedback
                } else if (weigh_option == 1) {
                    // Target - go to digit entry
                    first_entry_weigh = true;
                    after_target = ScreenId::Weigh;
                    current = ScreenId::SetTargetDigit;
                } else {
                    // Back to menu
                    first_entry_weigh = true;
                    current = ScreenId::Menu;
                }
            }
            was_pressed_weigh = pressed;

            sleep_ms(20);  // faster update for weighing
            break;
        }

        case ScreenId::Dispense:
        {
            // Dispense state machine
            // NOTE: Weight DECREASES as corn is dispensed from hanging bag
            enum class DispenseState { Idle, Running, Done };
            static DispenseState disp_state = DispenseState::Idle;
            static int disp_option = 0;  // 0=Target, 1=Start, 2=Back (Idle) or 0=Stop (Running) or 0=OK, 1=Retry (Done)
            static int last_disp_option = -1;
            static int last_encoder_pos_disp = 0;
            static bool first_entry_disp = true;
            static bool was_pressed_disp = false;

            // PID variables
            static double pid_input = 0.0;
            static double pid_output = 0.0;
            static double pid_setpoint = 0.0;
            static PID* dispense_pid = nullptr;
            static const double Kp = 2.0, Ki = 0.5, Kd = 0.1;  // Tune these!
            static const float TOLERANCE = 2.0f;  // grams tolerance for "done"
            static const float SERVO_CLOSED = 0.0f;
            static const float SERVO_OPEN = 90.0f;  // degrees when fully open

            // Track weight decrease
            static float start_weight = 0.0f;  // Weight when dispense started
            static float final_dispensed = 0.0f;  // Final amount dispensed (for Done screen)

            if (first_entry_disp) {
                last_encoder_pos_disp = enc.getPosition();
                first_entry_disp = false;
                was_pressed_disp = false;
                disp_state = DispenseState::Idle;
                disp_option = 1;  // Default to Start
                last_disp_option = -1;

                // Create PID if needed
                if (dispense_pid == nullptr) {
                    dispense_pid = new PID(&pid_input, &pid_output, &pid_setpoint,
                                          Kp, Ki, Kd, DIRECT);
                    dispense_pid->SetOutputLimits(0, 100);  // 0-100% opening
                    dispense_pid->SetSampleTime(50);
                }
            }

            // Read current weight from scale
            float current_grams = scales[selected_scale]->read_weight();
            // Calculate how much has been dispensed (weight decrease = positive dispensed)
            float dispensed_grams = start_weight - current_grams;
            int display_dispensed = (int)(dispensed_grams + 0.5f);
            if (display_dispensed < 0) display_dispensed = 0;  // Don't show negative

            // Handle encoder
            int pos = enc.getPosition();
            int delta = pos - last_encoder_pos_disp;
            bool pressed = enc.isPressed();

            switch (disp_state) {
            case DispenseState::Idle:
            {
                // Options: [Target] [Start] [Back]
                if (delta != 0) {
                    disp_option += delta;
                    if (disp_option < 0) disp_option = 0;
                    if (disp_option > 2) disp_option = 2;
                    last_encoder_pos_disp = pos;
                }

                // Update display
                if (disp_option != last_disp_option) {
                    const char* opts[3] = {"Target", "Start", "Back"};
                    char opt_line[21];
                    std::snprintf(opt_line, sizeof(opt_line), "%s%s%s%s%s%s%s%s%s",
                        disp_option == 0 ? "[" : " ", opts[0], disp_option == 0 ? "]" : " ",
                        disp_option == 1 ? "[" : " ", opts[1], disp_option == 1 ? "]" : " ",
                        disp_option == 2 ? "[" : " ", opts[2], disp_option == 2 ? "]" : " ");
                    lcd.setCursor(3, 0);
                    lcd.print(opt_line);
                    last_disp_option = disp_option;
                }

                // Show current state on LCD
                char line[21];
                std::snprintf(line, sizeof(line), "Target: %d g    ", target_grams);
                lcd.setCursor(1, 0);
                lcd.print(line);
                int display_current = (int)(current_grams + 0.5f);
                std::snprintf(line, sizeof(line), "Current: %d g   ", display_current);
                lcd.setCursor(2, 0);
                lcd.print(line);

                // 7-segment shows target
                sevenSeg->clear();
                sevenSeg->printNumber(target_grams, 0, 255, 0);
                sevenSeg->show();

                // Handle button
                if (pressed && !was_pressed_disp) {
                    if (disp_option == 0) {
                        // Set target - go to digit entry, come back here
                        first_entry_disp = true;
                        after_target = ScreenId::Dispense;
                        current = ScreenId::SetTargetDigit;
                    } else if (disp_option == 1) {
                        // Start dispensing - first tare the scale
                        lcd.setCursor(2, 0);
                        lcd.print("Taring...           ");
                        scales[selected_scale]->tare();
                        sleep_ms(500);  // Wait for tare to settle

                        // Now read the tared weight (should be ~0)
                        start_weight = scales[selected_scale]->read_weight();
                        pid_setpoint = (double)target_grams;  // Target amount to dispense
                        pid_input = 0.0;  // Start with 0 dispensed
                        dispense_pid->SetMode(AUTOMATIC);
                        disp_state = DispenseState::Running;
                        disp_option = 0;
                        last_disp_option = -1;
                        vibrators[selected_scale]->on();
                        lcd.setCursor(3, 0);
                        lcd.print("   [Stop]           ");
                    } else {
                        // Back to menu
                        first_entry_disp = true;
                        current = ScreenId::Menu;
                    }
                }
                break;
            }

            case DispenseState::Running:
            {
                // PID control - input is dispensed amount, setpoint is target
                pid_input = (double)dispensed_grams;
                dispense_pid->Compute();

                // Map PID output (0-100) to servo angle
                float servo_angle = SERVO_OPEN * (float)(pid_output / 100.0);
                // Close proportionally as we approach target
                float remaining = (float)target_grams - dispensed_grams;
                if (remaining < 20) {
                    // Close proportionally as we get close
                    servo_angle = SERVO_OPEN * (remaining / 20.0f);
                    if (servo_angle < 5.0f) servo_angle = 5.0f;  // minimum crack
                }
                if (remaining <= 0) servo_angle = SERVO_CLOSED;

                servos[selected_scale]->writeDegrees(servo_angle);

                // Update display
                char line[21];
                std::snprintf(line, sizeof(line), "Target: %d g    ", target_grams);
                lcd.setCursor(1, 0);
                lcd.print(line);
                std::snprintf(line, sizeof(line), "Dispensing: %d g", display_dispensed);
                lcd.setCursor(2, 0);
                lcd.print(line);

                // 7-segment shows dispensed amount with color
                sevenSeg->clear();
                if (dispensed_grams < target_grams - 5) {
                    sevenSeg->printNumber(display_dispensed, 0, 0, 255);  // blue - still dispensing
                } else if (dispensed_grams > target_grams + 5) {
                    sevenSeg->printNumber(display_dispensed, 255, 0, 0);  // red - overshoot!
                } else {
                    sevenSeg->printNumber(display_dispensed, 0, 255, 0);  // green - on target
                }
                sevenSeg->show();

                // Check if done (dispensed enough)
                if (dispensed_grams >= target_grams - TOLERANCE) {
                    // Done!
                    final_dispensed = dispensed_grams;  // Remember final amount
                    servos[selected_scale]->writeDegrees(SERVO_CLOSED);
                    vibrators[selected_scale]->off();
                    dispense_pid->SetMode(MANUAL);
                    disp_state = DispenseState::Done;
                    disp_option = 0;
                    last_disp_option = -1;
                    bz.beep();
                }

                // Manual stop
                if (pressed && !was_pressed_disp) {
                    servos[selected_scale]->writeDegrees(SERVO_CLOSED);
                    vibrators[selected_scale]->off();
                    dispense_pid->SetMode(MANUAL);
                    disp_state = DispenseState::Idle;
                    disp_option = 1;
                    last_disp_option = -1;
                }
                break;
            }

            case DispenseState::Done:
            {
                // Options: [OK] [Retry]
                if (delta != 0) {
                    disp_option += delta;
                    if (disp_option < 0) disp_option = 0;
                    if (disp_option > 1) disp_option = 1;
                    last_encoder_pos_disp = pos;
                }

                if (disp_option != last_disp_option) {
                    char opt_line[21];
                    std::snprintf(opt_line, sizeof(opt_line), "   %s%s%s   %s%s%s   ",
                        disp_option == 0 ? "[" : " ", "OK", disp_option == 0 ? "]" : " ",
                        disp_option == 1 ? "[" : " ", "Retry", disp_option == 1 ? "]" : " ");
                    lcd.setCursor(3, 0);
                    lcd.print(opt_line);
                    last_disp_option = disp_option;
                }

                // Show result
                char line[21];
                std::snprintf(line, sizeof(line), "Target: %d g    ", target_grams);
                lcd.setCursor(1, 0);
                lcd.print(line);
                int display_final = (int)(final_dispensed + 0.5f);
                std::snprintf(line, sizeof(line), "Dispensed: %d g ", display_final);
                lcd.setCursor(2, 0);
                lcd.print(line);

                // Green on 7-segment
                sevenSeg->clear();
                sevenSeg->printNumber(display_final, 0, 255, 0);
                sevenSeg->show();

                if (pressed && !was_pressed_disp) {
                    if (disp_option == 0) {
                        // OK - back to menu
                        first_entry_disp = true;
                        current = ScreenId::Menu;
                    } else {
                        // Retry - tare and restart
                        scales[selected_scale]->tare();
                        disp_state = DispenseState::Idle;
                        disp_option = 1;
                        last_disp_option = -1;
                    }
                }
                break;
            }
            }

            was_pressed_disp = pressed;
            sleep_ms(20);
            break;
        }

        case ScreenId::TestVibrator:
        {
            // Vibrator test screen - select vibrator and control intensity with encoder
            static int test_vib_selected = 0;  // 0, 1, 2 = vibrator index
            static int test_vib_intensity = 0;  // 0-100%
            static int last_encoder_pos_test = 0;
            static bool first_entry_test = true;
            static bool was_pressed_test = false;
            static bool adjusting_intensity = false;

            if (first_entry_test) {
                last_encoder_pos_test = enc.getPosition();
                first_entry_test = false;
                was_pressed_test = false;
                test_vib_selected = 0;
                test_vib_intensity = 0;
                adjusting_intensity = false;
                // Turn off all vibrators
                for (int i = 0; i < 3; i++) {
                    vibrators[i]->off();
                }
            }

            int pos = enc.getPosition();
            int delta = pos - last_encoder_pos_test;
            bool pressed = enc.isPressed();

            if (adjusting_intensity) {
                // Encoder changes intensity
                if (delta != 0) {
                    test_vib_intensity += delta * 5;  // 5% steps
                    if (test_vib_intensity < 0) test_vib_intensity = 0;
                    if (test_vib_intensity > 100) test_vib_intensity = 100;
                    last_encoder_pos_test = pos;
                    // Apply intensity to selected vibrator
                    vibrators[test_vib_selected]->setIntensity((float)test_vib_intensity / 100.0f);
                }

                // Button exits intensity mode
                if (pressed && !was_pressed_test) {
                    adjusting_intensity = false;
                    // Turn off vibrator when exiting
                    vibrators[test_vib_selected]->off();
                    test_vib_intensity = 0;
                }
            } else {
                // Encoder selects vibrator (0, 1, 2) or Back (3)
                if (delta != 0) {
                    test_vib_selected += delta;
                    if (test_vib_selected < 0) test_vib_selected = 0;
                    if (test_vib_selected > 3) test_vib_selected = 3;
                    last_encoder_pos_test = pos;
                }

                // Button enters intensity mode or goes back
                if (pressed && !was_pressed_test) {
                    if (test_vib_selected < 3) {
                        adjusting_intensity = true;
                        test_vib_intensity = 0;
                    } else {
                        // Back to menu
                        first_entry_test = true;
                        current = ScreenId::Menu;
                    }
                }
            }
            was_pressed_test = pressed;

            // Update display
            char line[21];
            if (adjusting_intensity) {
                std::snprintf(line, sizeof(line), "Vibrator %d: %3d%%   ", test_vib_selected + 1, test_vib_intensity);
                lcd.setCursor(2, 0);
                lcd.print(line);
                lcd.setCursor(3, 0);
                lcd.print("  Turn to adjust    ");
            } else {
                // Show selection
                std::snprintf(line, sizeof(line), "%s1%s %s2%s %s3%s %sBack%s",
                    test_vib_selected == 0 ? "[" : " ", test_vib_selected == 0 ? "]" : " ",
                    test_vib_selected == 1 ? "[" : " ", test_vib_selected == 1 ? "]" : " ",
                    test_vib_selected == 2 ? "[" : " ", test_vib_selected == 2 ? "]" : " ",
                    test_vib_selected == 3 ? "[" : " ", test_vib_selected == 3 ? "]" : " ");
                lcd.setCursor(2, 0);
                lcd.print(line);
                lcd.setCursor(3, 0);
                lcd.print("Press to test       ");
            }

            // Show intensity on 7-segment
            sevenSeg->clear();
            if (adjusting_intensity) {
                // Show intensity in color (blue->green->red)
                uint8_t r = test_vib_intensity > 50 ? (test_vib_intensity - 50) * 5 : 0;
                uint8_t g = test_vib_intensity < 50 ? test_vib_intensity * 5 : (100 - test_vib_intensity) * 5;
                uint8_t b = test_vib_intensity < 50 ? (50 - test_vib_intensity) * 5 : 0;
                sevenSeg->printNumber(test_vib_intensity, r, g, b);
            } else {
                sevenSeg->printNumber(test_vib_selected + 1, 0, 255, 0);
            }
            sevenSeg->show();

            sleep_ms(50);
            break;
        }
        }
    }
}
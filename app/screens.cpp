#include "screens.hpp"

#include <cstdio>
#include <cstdlib>
#include "pico/stdlib.h"

#include "Lcd1602I2C.hpp"
#include "Rotary_Button.hpp"
#include "Buzzer.hpp"
#include "hx711.hpp"
#include "config_store.hpp"
#include "Servo.hpp"
#include "Vibrator.hpp"
#include "SevenSeg.hpp"
#include "PID.hpp"
#include "telemetry.hpp"
#include "dispenser_state.h"

// Menu arrow indicator (supports up to 4 rows)
static void indicatorArrow(Lcd1602I2C& lcd, int lineNumber, int startRow = 0, int count = 3) {
    for (int i = 0; i < count; i++) {
        lcd.setCursor(startRow + i, 0);
        lcd.writeChar(i == lineNumber ? '>' : ' ');
    }
}

// ---------------------------------------------------------------- Menu ------

class MenuScreen : public Screen {
    int last_selected_ = -1;
public:
    void enter(UiContext& ctx) override {
        ctx.lcd.clear();
        ctx.lcd.setCursor(0, 2);
        ctx.lcd.print("Calibrate");
        ctx.lcd.setCursor(1, 2);
        ctx.lcd.print("Dispense");
        ctx.lcd.setCursor(2, 2);
        ctx.lcd.print("Weigh");
        ctx.lcd.setCursor(3, 2);
        ctx.lcd.print("Test");
        indicatorArrow(ctx.lcd, 0, 0, 4);
        last_selected_ = 0;
    }

    ScreenId update(UiContext& ctx) override {
        // robust mod for negatives (4 menu items)
        int pos = ctx.enc.getPosition();
        int selected = ((pos % 4) + 4) % 4;

        if (selected != last_selected_) {
            indicatorArrow(ctx.lcd, selected, 0, 4);
            last_selected_ = selected;
        }

        if (ctx.enc.isPressed()) {
            if (selected == 0) {
                ctx.after_select = ScreenId::Calibrate1;
                return ScreenId::SelectScale;
            } else if (selected == 1) {
                ctx.after_select = ScreenId::Dispense;
                return ScreenId::SelectScale;
            } else if (selected == 2) {
                ctx.after_select = ScreenId::Weigh;
                return ScreenId::SelectScale;
            } else {
                return ScreenId::TestMenu;
            }
        }
        return ScreenId::Menu;
    }
};

// ---------------------------------------------------------- SelectScale -----

class SelectScaleScreen : public Screen {
    int last_selected_ = -1;
public:
    void enter(UiContext& ctx) override {
        ctx.lcd.clear();
        ctx.lcd.setCursor(0, 0);
        ctx.lcd.print("Select Scale:");
        for (int i = 0; i < 3; i++) {
            char row[21];
            if (ctx.names && ctx.names[i][0]) {
                std::snprintf(row, sizeof(row), "%d: %.15s", i + 1, ctx.names[i]);
            } else {
                std::snprintf(row, sizeof(row), "Scale %d", i + 1);
            }
            ctx.lcd.setCursor(1 + i, 2);
            ctx.lcd.print(row);
        }
        indicatorArrow(ctx.lcd, 0, 1, 3);  // Arrow on rows 1-3
        last_selected_ = 0;
    }

    ScreenId update(UiContext& ctx) override {
        int pos = ctx.enc.getPosition();
        int selected = ((pos % 3) + 3) % 3;

        if (selected != last_selected_) {
            indicatorArrow(ctx.lcd, selected, 1, 3);
            last_selected_ = selected;
        }

        if (ctx.enc.isPressed()) {
            ctx.selected_scale = selected;   // 0, 1, or 2
            return ctx.after_select;         // Calibrate1, Dispense or Weigh
        }
        return ScreenId::SelectScale;
    }
};

// ----------------------------------------------------------- Calibrate1 -----

class Calibrate1Screen : public Screen {
    int  option_ = 0;        // 0=Tare, 1=Back
    int  last_option_ = -1;
    int  last_encoder_pos_ = 0;
    bool was_pressed_ = false;
public:
    void enter(UiContext& ctx) override {
        ctx.lcd.clear();
        char title[21];
        std::snprintf(title, sizeof(title), "Calibrate Scale %d", ctx.selected_scale + 1);
        ctx.lcd.setCursor(0, 0);
        ctx.lcd.print(title);
        ctx.lcd.setCursor(1, 0);
        ctx.lcd.print("Remove all weight   ");
        ctx.lcd.setCursor(2, 0);
        ctx.lcd.print("then select:        ");
        // Row 3 options are drawn in update()

        last_encoder_pos_ = ctx.enc.getPosition();
        was_pressed_ = ctx.enc.isPressed();
        option_ = 0;
        last_option_ = -1;
    }

    ScreenId update(UiContext& ctx) override {
        int pos = ctx.enc.getPosition();
        int delta = pos - last_encoder_pos_;
        if (delta != 0) {
            option_ += delta;
            if (option_ < 0) option_ = 0;
            if (option_ > 1) option_ = 1;
            last_encoder_pos_ = pos;
        }

        if (option_ != last_option_) {
            char opt_line[21];
            std::snprintf(opt_line, sizeof(opt_line), "   %s%s%s%s%s%s",
                option_ == 0 ? "[" : " ", "Tare", option_ == 0 ? "]" : " ",
                option_ == 1 ? "[" : " ", "Back", option_ == 1 ? "]" : " ");
            ctx.lcd.setCursor(3, 0);
            ctx.lcd.print(opt_line);
            last_option_ = option_;
        }

        bool pressed = ctx.enc.isPressed();
        if (pressed && !was_pressed_) {
            if (option_ == 0) {
                // Tare and continue
                ctx.scales[ctx.selected_scale]->tare();
                ctx.bz.playMarioCoin();  // Bling!
                was_pressed_ = pressed;
                return ScreenId::Calibrate2;
            } else {
                was_pressed_ = pressed;
                return ScreenId::Menu;
            }
        }
        was_pressed_ = pressed;

        sleep_ms(50);
        return ScreenId::Calibrate1;
    }
};

// ----------------------------------------------------------- Calibrate2 -----

class Calibrate2Screen : public Screen {
    // digits_ intentionally persists across visits (remembers the last known
    // calibration weight), matching the old static behavior
    int  digits_[4] = {1, 0, 5, 6};  // Default 1056g
    int  cursor_pos_ = 0;            // 0-3 = digits, 4 = OK, 5 = Back
    int  last_encoder_pos_ = 0;
    bool was_pressed_ = false;
    bool edit_mode_ = false;
public:
    void enter(UiContext& ctx) override {
        ctx.lcd.clear();
        char title[21];
        std::snprintf(title, sizeof(title), "Scale %d - Step 2", ctx.selected_scale + 1);
        ctx.lcd.setCursor(0, 0);
        ctx.lcd.print(title);
        ctx.lcd.setCursor(1, 0);
        ctx.lcd.print("Place known weight");

        last_encoder_pos_ = ctx.enc.getPosition();
        was_pressed_ = ctx.enc.isPressed();
        cursor_pos_ = 0;
        edit_mode_ = false;
    }

    ScreenId update(UiContext& ctx) override {
        bool pressed = ctx.enc.isPressed();
        int pos = ctx.enc.getPosition();
        int delta = pos - last_encoder_pos_;
        ScreenId next = ScreenId::Calibrate2;

        if (edit_mode_) {
            // Edit mode: encoder changes digit value
            if (delta != 0) {
                digits_[cursor_pos_] = ((digits_[cursor_pos_] + delta) % 10 + 10) % 10;
                last_encoder_pos_ = pos;
            }
            if (pressed && !was_pressed_) {
                edit_mode_ = false;
            }
        } else {
            // Navigation mode: encoder moves cursor
            if (delta != 0) {
                cursor_pos_ += delta;
                if (cursor_pos_ < 0) cursor_pos_ = 0;
                if (cursor_pos_ > 5) cursor_pos_ = 5;  // 0-3=digits, 4=OK, 5=Back
                last_encoder_pos_ = pos;
            }
            if (pressed && !was_pressed_) {
                if (cursor_pos_ < 4) {
                    edit_mode_ = true;
                } else if (cursor_pos_ == 4) {
                    // OK pressed - calibrate selected scale
                    int known_grams = digits_[0] * 1000 + digits_[1] * 100 +
                                      digits_[2] * 10 + digits_[3];
                    if (known_grams < 1) known_grams = 1;

                    ctx.scales[ctx.selected_scale]->calibrate_scale((float)known_grams, 10);
                    // The tare done in step 1 IS the calibrated zero
                    ctx.scales[ctx.selected_scale]->set_cal_offset(
                        ctx.scales[ctx.selected_scale]->get_offset());

                    // Save to flash (entry index matches scale index)
                    ctx.sc.entries[ctx.selected_scale].offset_counts =
                        ctx.scales[ctx.selected_scale]->get_offset();
                    ctx.sc.entries[ctx.selected_scale].count_per_g =
                        ctx.scales[ctx.selected_scale]->get_scale();
                    save_scale_config(ctx.sc);

                    next = ScreenId::Menu;
                } else {
                    // Back pressed - return to menu without saving
                    next = ScreenId::Menu;
                }
            }
        }
        was_pressed_ = pressed;

        // Display with edit indicator: brackets around digit being edited
        char line1[21];
        if (edit_mode_) {
            char d[4][4];
            for (int i = 0; i < 4; i++) {
                if (i == cursor_pos_) {
                    std::snprintf(d[i], 4, "[%d]", digits_[i]);
                } else {
                    std::snprintf(d[i], 4, " %d ", digits_[i]);
                }
            }
            std::snprintf(line1, sizeof(line1), "%s%s%s%sg", d[0], d[1], d[2], d[3]);
        } else {
            std::snprintf(line1, sizeof(line1), "%d %d %d %d g %s%s",
                         digits_[0], digits_[1], digits_[2], digits_[3],
                         cursor_pos_ == 4 ? "[OK]" : " OK ",
                         cursor_pos_ == 5 ? "[Back]" : " Back ");
        }
        ctx.lcd.setCursor(2, 0);
        ctx.lcd.print(line1);

        // Cursor line (only in navigation mode)
        char line2[21] = "                    ";
        if (!edit_mode_ && cursor_pos_ < 4) {
            int cursor_x = cursor_pos_ * 2;
            line2[cursor_x] = '^';
        }
        ctx.lcd.setCursor(3, 0);
        ctx.lcd.print(line2);

        sleep_ms(50);
        return next;
    }
};

// ------------------------------------------------------------ SetTarget -----
// (Not currently reachable from any menu - kept for completeness)

class SetTargetScreen : public Screen {
    int last_encoder_pos_ = 0;
public:
    void enter(UiContext& ctx) override {
        ctx.lcd.clear();
        ctx.lcd.setCursor(0, 0);
        ctx.lcd.print("Set Target Weight");
        ctx.lcd.setCursor(1, 0);
        ctx.lcd.print("Use encoder to adjust");
        ctx.lcd.setCursor(2, 0);
        ctx.lcd.print("Press to confirm");

        last_encoder_pos_ = ctx.enc.getPosition();
    }

    ScreenId update(UiContext& ctx) override {
        int pos = ctx.enc.getPosition();
        int delta = pos - last_encoder_pos_;
        if (delta != 0) {
            ctx.target_grams += delta;
            if (ctx.target_grams < 1) ctx.target_grams = 1;
            if (ctx.target_grams > 9999) ctx.target_grams = 9999;
            last_encoder_pos_ = pos;
        }

        ctx.sevenSeg->clear();
        ctx.sevenSeg->printNumber(ctx.target_grams, 0, 255, 0);  // green
        ctx.sevenSeg->show();

        char line[21];
        std::snprintf(line, sizeof(line), "Target: %d g    ", ctx.target_grams);
        ctx.lcd.setCursor(3, 0);
        ctx.lcd.print(line);

        if (ctx.enc.isPressed()) {
            return ScreenId::Weigh;
        }

        sleep_ms(50);
        return ScreenId::SetTarget;
    }
};

// ------------------------------------------------------- SetTargetDigit -----

class SetTargetDigitScreen : public Screen {
    int  digits_[4] = {0, 1, 0, 0};
    int  cursor_pos_ = 0;   // 0-3 = digits, 4 = OK
    int  last_encoder_pos_ = 0;
    bool was_pressed_ = false;
    bool edit_mode_ = false;
public:
    void enter(UiContext& ctx) override {
        ctx.lcd.clear();
        ctx.lcd.setCursor(0, 0);
        ctx.lcd.print("Set Target Weight");
        ctx.lcd.setCursor(1, 0);
        ctx.lcd.print("Enter grams:");

        // Initialize digits from current target
        digits_[0] = (ctx.target_grams / 1000) % 10;
        digits_[1] = (ctx.target_grams / 100) % 10;
        digits_[2] = (ctx.target_grams / 10) % 10;
        digits_[3] = ctx.target_grams % 10;
        last_encoder_pos_ = ctx.enc.getPosition();
        was_pressed_ = false;
        cursor_pos_ = 0;
        edit_mode_ = false;
    }

    ScreenId update(UiContext& ctx) override {
        bool pressed = ctx.enc.isPressed();
        int pos = ctx.enc.getPosition();
        int delta = pos - last_encoder_pos_;
        ScreenId next = ScreenId::SetTargetDigit;

        if (edit_mode_) {
            if (delta != 0) {
                digits_[cursor_pos_] = ((digits_[cursor_pos_] + delta) % 10 + 10) % 10;
                last_encoder_pos_ = pos;
            }
            if (pressed && !was_pressed_) {
                edit_mode_ = false;
            }
        } else {
            if (delta != 0) {
                cursor_pos_ += delta;
                if (cursor_pos_ < 0) cursor_pos_ = 0;
                if (cursor_pos_ > 4) cursor_pos_ = 4;
                last_encoder_pos_ = pos;
            }
            if (pressed && !was_pressed_) {
                if (cursor_pos_ < 4) {
                    edit_mode_ = true;
                } else {
                    // OK pressed - set target and return to caller
                    ctx.target_grams = digits_[0] * 1000 + digits_[1] * 100 +
                                       digits_[2] * 10 + digits_[3];
                    if (ctx.target_grams < 1) ctx.target_grams = 1;
                    next = ctx.after_target;
                }
            }
        }
        was_pressed_ = pressed;

        // Display with edit indicator
        char line1[21];
        if (edit_mode_) {
            char d[4][4];
            for (int i = 0; i < 4; i++) {
                if (i == cursor_pos_) {
                    std::snprintf(d[i], 4, "[%d]", digits_[i]);
                } else {
                    std::snprintf(d[i], 4, " %d ", digits_[i]);
                }
            }
            std::snprintf(line1, sizeof(line1), "%s%s%s%s OK",
                         d[0], d[1], d[2], d[3]);
        } else {
            std::snprintf(line1, sizeof(line1), " %d  %d  %d  %d %s",
                         digits_[0], digits_[1], digits_[2], digits_[3],
                         cursor_pos_ == 4 ? "[OK]" : " OK ");
        }
        ctx.lcd.setCursor(2, 0);
        ctx.lcd.print(line1);

        // Cursor line (only in navigation mode)
        char line2[21] = "                    ";
        if (!edit_mode_) {
            int cursor_x = 1 + cursor_pos_ * 3;
            if (cursor_pos_ == 4) cursor_x = 13;
            line2[cursor_x] = '^';
        }
        ctx.lcd.setCursor(3, 0);
        ctx.lcd.print(line2);

        // Show current value on 7-segment
        int preview = digits_[0] * 1000 + digits_[1] * 100 +
                      digits_[2] * 10 + digits_[3];
        ctx.sevenSeg->clear();
        ctx.sevenSeg->printNumber(preview, 0, 255, 0);  // green
        ctx.sevenSeg->show();

        sleep_ms(50);
        return next;
    }
};

// ---------------------------------------------------------------- Weigh -----

class WeighScreen : public Screen {
    int  option_ = 0;        // 0=Tare, 1=Target, 2=Back
    int  last_option_ = -1;
    int  last_encoder_pos_ = 0;
    bool was_pressed_ = false;
public:
    void enter(UiContext& ctx) override {
        ctx.lcd.clear();
        char title[21];
        if (ctx.names && ctx.names[ctx.selected_scale][0]) {
            std::snprintf(title, sizeof(title), "Weigh %d: %.11s",
                          ctx.selected_scale + 1, ctx.names[ctx.selected_scale]);
        } else {
            std::snprintf(title, sizeof(title), "Weighing - Scale %d", ctx.selected_scale + 1);
        }
        ctx.lcd.setCursor(0, 0);
        ctx.lcd.print(title);

        last_encoder_pos_ = ctx.enc.getPosition();
        was_pressed_ = false;
        option_ = 0;
        last_option_ = -1;
    }

    ScreenId update(UiContext& ctx) override {
        // Read weight from selected scale
        float grams = ctx.scales[ctx.selected_scale]->read_weight();
        int display_grams = (int)(grams + 0.5f);  // round to nearest gram

        // Display on 7-segment (color based on target)
        ctx.sevenSeg->clear();
        if (display_grams < ctx.target_grams - 5) {
            ctx.sevenSeg->printNumber(display_grams, 0, 0, 255);   // under: blue
        } else if (display_grams > ctx.target_grams + 5) {
            ctx.sevenSeg->printNumber(display_grams, 255, 0, 0);   // over: red
        } else {
            ctx.sevenSeg->printNumber(display_grams, 0, 255, 0);   // at target: green
        }
        ctx.sevenSeg->show();

        char line[21];
        std::snprintf(line, sizeof(line), "Weight: %d g    ", display_grams);
        ctx.lcd.setCursor(1, 0);
        ctx.lcd.print(line);
        std::snprintf(line, sizeof(line), "Target: %d g    ", ctx.target_grams);
        ctx.lcd.setCursor(2, 0);
        ctx.lcd.print(line);

        int pos = ctx.enc.getPosition();
        int delta = pos - last_encoder_pos_;
        if (delta != 0) {
            option_ += delta;
            if (option_ < 0) option_ = 0;
            if (option_ > 2) option_ = 2;
            last_encoder_pos_ = pos;
        }

        if (option_ != last_option_) {
            char opt_line[21];
            std::snprintf(opt_line, sizeof(opt_line), "%s%s%s%s%s%s%s%s%s",
                option_ == 0 ? "[" : " ", "Tare", option_ == 0 ? "]" : " ",
                option_ == 1 ? "[" : " ", "Target", option_ == 1 ? "]" : " ",
                option_ == 2 ? "[" : " ", "Back", option_ == 2 ? "]" : " ");
            ctx.lcd.setCursor(3, 0);
            ctx.lcd.print(opt_line);
            last_option_ = option_;
        }

        bool pressed = ctx.enc.isPressed();
        ScreenId next = ScreenId::Weigh;
        if (pressed && !was_pressed_) {
            if (option_ == 0) {
                // Tare - zero the scale
                ctx.scales[ctx.selected_scale]->tare();
                ctx.bz.playMarioCoin();  // Bling!
            } else if (option_ == 1) {
                // Target - go to digit entry, come back here
                ctx.after_target = ScreenId::Weigh;
                next = ScreenId::SetTargetDigit;
            } else {
                next = ScreenId::Menu;
            }
        }
        was_pressed_ = pressed;

        sleep_ms(20);  // faster update for weighing
        return next;
    }
};

// -------------------------------------------------------------- Dispense ----

class DispenseScreen : public Screen {
    enum class DispenseState { Idle, Running, Done };

    // Servo working range comes from the per-servo zero calibration
    // (servo_min_open / servo_close in dispenser_state.h)

    DispenseState state_ = DispenseState::Idle;
    int  option_ = 1;        // 0=Target, 1=Start, 2=Back (Idle) / 0=Back, 1=Retry (Done)
    int  last_option_ = -1;
    int  last_encoder_pos_ = 0;
    bool was_pressed_ = false;

    // PID variables - the PID object holds pointers to these, so this screen
    // must live in static storage (it does: singleton below)
    double pid_input_ = 0.0;
    double pid_output_ = 0.0;
    double pid_setpoint_ = 0.0;

    // NOTE: Weight DECREASES as corn is dispensed from the hanging bag
    float start_weight_ = 0.0f;      // Weight when dispense started
    float final_dispensed_ = 0.0f;   // Final amount dispensed (for Done display)
    uint32_t dispense_start_ms_ = 0; // For telemetry timestamps

    // Last values painted on the LCD. Repainting only on change matters: even
    // with fast I2C a 16-char line costs ~9 ms, and unconditional repaints
    // every loop tick were the main reason the PID ran at ~200 ms instead of
    // its 100 ms sample time.
    int lcd_target_ = -1;
    int lcd_value_  = -1;
    void resetLcdCache() { lcd_target_ = -1; lcd_value_ = -1; }

public:
    void enter(UiContext& ctx) override {
        ctx.lcd.clear();
        char title[21];
        if (ctx.names && ctx.names[ctx.selected_scale][0]) {
            std::snprintf(title, sizeof(title), "Disp %d: %.12s",
                          ctx.selected_scale + 1, ctx.names[ctx.selected_scale]);
        } else {
            std::snprintf(title, sizeof(title), "Dispense - Scale %d", ctx.selected_scale + 1);
        }
        ctx.lcd.setCursor(0, 0);
        ctx.lcd.print(title);

        last_encoder_pos_ = ctx.enc.getPosition();
        was_pressed_ = ctx.enc.isPressed();  // ignore carry-over press
        state_ = DispenseState::Idle;
        option_ = 1;  // Default to Start
        last_option_ = -1;
        resetLcdCache();

        // Create PID if needed (output limits are set per dispense start -
        // they depend on which scale's servo runs)
        if (ctx.dispense_pid == nullptr) {
            ctx.dispense_pid = new PID(&pid_input_, &pid_output_, &pid_setpoint_,
                                       ctx.Kp, ctx.Ki, ctx.Kd, DIRECT);
            // 100 ms matches the HX711's ~10 SPS so every PID compute sees a
            // genuinely new sample
            ctx.dispense_pid->SetSampleTime(100);
        }
    }

    ScreenId update(UiContext& ctx) override {
        // Freshest-available non-blocking read (a multi-sample read would block
        // and throttle the PID loop)
        float current_grams = ctx.scales[ctx.selected_scale]->read_weight();
        float current_gross = ctx.scales[ctx.selected_scale]->last_gross();
        // Weight decrease = positive dispensed
        float dispensed_grams = start_weight_ - current_grams;
        int display_dispensed = (int)(dispensed_grams + 0.5f);
        if (display_dispensed < 0) display_dispensed = 0;

        int pos = ctx.enc.getPosition();
        int delta = pos - last_encoder_pos_;
        bool pressed = ctx.enc.isPressed();
        ScreenId next = ScreenId::Dispense;

        switch (state_) {
        case DispenseState::Idle:
        {
            // Options: [Target] [Start] [Back]
            if (delta != 0) {
                option_ += delta;
                if (option_ < 0) option_ = 0;
                if (option_ > 2) option_ = 2;
                last_encoder_pos_ = pos;
            }

            if (option_ != last_option_) {
                const char* opts[3] = {"Target", "Start", "Back"};
                char opt_line[21];
                std::snprintf(opt_line, sizeof(opt_line), "%s%s%s%s%s%s%s%s%s",
                    option_ == 0 ? "[" : " ", opts[0], option_ == 0 ? "]" : " ",
                    option_ == 1 ? "[" : " ", opts[1], option_ == 1 ? "]" : " ",
                    option_ == 2 ? "[" : " ", opts[2], option_ == 2 ? "]" : " ");
                ctx.lcd.setCursor(3, 0);
                ctx.lcd.print(opt_line);
                last_option_ = option_;
            }

            char line[21];
            if (ctx.target_grams != lcd_target_) {
                std::snprintf(line, sizeof(line), "Target: %d g    ", ctx.target_grams);
                ctx.lcd.setCursor(1, 0);
                ctx.lcd.print(line);
                lcd_target_ = ctx.target_grams;
            }
            int display_current = (int)(current_grams + 0.5f);
            if (display_current != lcd_value_) {
                std::snprintf(line, sizeof(line), "Current: %d g   ", display_current);
                ctx.lcd.setCursor(2, 0);
                ctx.lcd.print(line);
                lcd_value_ = display_current;
            }

            // 7-segment shows target
            ctx.sevenSeg->clear();
            ctx.sevenSeg->printNumber(ctx.target_grams, 0, 255, 0);
            ctx.sevenSeg->show();

            // Handle button or web-initiated start
            bool do_start = ctx.web_start_dispense;
            ctx.web_start_dispense = false;

            if (pressed && !was_pressed_) {
                if (option_ == 0) {
                    // Set target - go to digit entry, come back here
                    ctx.after_target = ScreenId::Dispense;
                    next = ScreenId::SetTargetDigit;
                } else if (option_ == 1) {
                    do_start = true;
                } else {
                    next = ScreenId::Menu;
                }
            }
            if (do_start) {
                ctx.web_stop_dispense = false;  // Clear any stale stop request
                // Start dispensing - first tare the scale
                ctx.lcd.setCursor(2, 0);
                ctx.lcd.print("Taring...           ");
                ctx.scales[ctx.selected_scale]->tare();
                ctx.bz.playMarioCoin();
                sleep_ms(300);

                start_weight_ = ctx.scales[ctx.selected_scale]->read_weight(3);
                pid_setpoint_ = (double)ctx.target_grams;
                pid_input_ = 0.0;
                // Working range of THIS scale's servo: calibrated zero up to
                // zero + 80 deg (mechanical end stop ~75 deg past zero), or the
                // 85-170 default. Set here, not in enter(): the web side can
                // switch scales while this screen idles.
                ctx.dispense_pid->SetOutputLimits(
                    servo_min_open(ctx.g_state, ctx.selected_scale),
                    servo_max_open(ctx.g_state, ctx.selected_scale));
                // Seed the output at the floor before enabling: SetMode's
                // bumpless transfer latches the CURRENT output into the
                // integrator, and with a tiny Ki a stale value from the last
                // run never bleeds off - the gate then rides ~50 deg above
                // the floor for the whole run (CSV runs 2-4: i_term 150-175).
                // Seeded at the floor, the end-phase tapers to just above the
                // flow-start point: angle = zero + Kp * grams_remaining.
                pid_output_ = (double)servo_min_open(ctx.g_state, ctx.selected_scale);
                ctx.dispense_pid->SetMode(AUTOMATIC);

                // Start telemetry capture (under lwIP lock so a CSV download
                // in flight can't observe the buffer reset mid-row)
                dispense_start_ms_ = to_ms_since_boot(get_absolute_time());
                ctx.net_lock();
                telem_begin_run((uint8_t)ctx.selected_scale, (uint16_t)ctx.target_grams,
                                (float)ctx.Kp, (float)ctx.Ki, (float)ctx.Kd,
                                ctx.names ? ctx.names[ctx.selected_scale] : "");
                ctx.net_unlock();

                state_ = DispenseState::Running;
                option_ = 0;
                last_option_ = -1;
                resetLcdCache();
                ctx.net_lock();
                ctx.g_state.dispensing = true;
                ctx.g_state.dispense_done = false;
                ctx.g_state.dispensed_grams = 0;
                ctx.net_unlock();
                ctx.lcd.setCursor(3, 0);
                ctx.lcd.print("   [Stop]           ");
            }
            break;
        }

        case DispenseState::Running:
        {
            // PID control - input is dispensed amount, setpoint is target
            // PID output is servo angle directly (like Arduino)
            pid_input_ = (double)dispensed_grams;
            bool pid_computed = ctx.dispense_pid->Compute();

            float servo_angle = (float)pid_output_;

            // Vibrator: assist the tail of the run; starts early because
            // the motor takes a moment to spin up
            constexpr float VIB_ASSIST_REMAINING_G = 80.0f;
            float remaining = (float)ctx.target_grams - dispensed_grams;
            if (remaining <= VIB_ASSIST_REMAINING_G) {
                ctx.vibrators[ctx.selected_scale]->setIntensity(0.6f);
            } else {
                ctx.vibrators[ctx.selected_scale]->off();
            }

            ctx.servos[ctx.selected_scale]->writeDegrees(servo_angle);

            // Log a telemetry sample per actual PID computation (10 Hz)
            if (pid_computed) {
                TelemetrySample ts;
                ts.t_ms      = to_ms_since_boot(get_absolute_time()) - dispense_start_ms_;
                ts.setpoint  = (float)pid_setpoint_;
                ts.dispensed = dispensed_grams;
                ts.weight    = current_grams;
                ts.gross     = current_gross;
                ts.servo     = servo_angle;
                ts.p         = (float)ctx.dispense_pid->GetLastP();
                ts.i         = (float)ctx.dispense_pid->GetLastI();
                ts.d         = (float)ctx.dispense_pid->GetLastD();
                ts.vib       = (remaining <= VIB_ASSIST_REMAINING_G) ? 0.6f : 0.0f;
                telem_append(ts);
            }

            // Sync web state
            ctx.net_lock();
            ctx.g_state.dispensed_grams = dispensed_grams;
            ctx.g_state.servo_angle = servo_angle;
            ctx.g_state.vib_intensity = (remaining <= VIB_ASSIST_REMAINING_G) ? 0.6f : 0.0f;
            ctx.net_unlock();

            // Repaint only changed lines - LCD writes stall the control loop
            char line[21];
            if (ctx.target_grams != lcd_target_) {
                std::snprintf(line, sizeof(line), "Target: %d g    ", ctx.target_grams);
                ctx.lcd.setCursor(1, 0);
                ctx.lcd.print(line);
                lcd_target_ = ctx.target_grams;
            }
            if (display_dispensed != lcd_value_) {
                std::snprintf(line, sizeof(line), "Dispensing: %d g", display_dispensed);
                ctx.lcd.setCursor(2, 0);
                ctx.lcd.print(line);
                lcd_value_ = display_dispensed;
            }

            // 7-segment shows dispensed amount with color
            ctx.sevenSeg->clear();
            if (dispensed_grams < ctx.target_grams - 5) {
                ctx.sevenSeg->printNumber(display_dispensed, 0, 0, 255);  // blue - dispensing
            } else if (dispensed_grams > ctx.target_grams + 5) {
                ctx.sevenSeg->printNumber(display_dispensed, 255, 0, 0);  // red - overshoot!
            } else {
                ctx.sevenSeg->printNumber(display_dispensed, 0, 255, 0);  // green - on target
            }
            ctx.sevenSeg->show();

            // Check if done (dispensed enough)
            if (dispensed_grams >= (float)ctx.target_grams) {
                final_dispensed_ = dispensed_grams;
                float close_deg = servo_close(ctx.g_state, ctx.selected_scale);
                ctx.servos[ctx.selected_scale]->writeDegrees(close_deg);
                ctx.vibrators[ctx.selected_scale]->off();
                ctx.dispense_pid->SetMode(MANUAL);
                telem_end_run(final_dispensed_);
                state_ = DispenseState::Done;
                option_ = 0;
                last_option_ = -1;
                resetLcdCache();
                ctx.net_lock();
                ctx.g_state.dispensing = false;
                ctx.g_state.dispense_done = true;
                ctx.g_state.dispensed_grams = final_dispensed_;
                ctx.g_state.servo_angle = close_deg;
                ctx.g_state.vib_intensity = 0.0f;
                ctx.net_unlock();
                ctx.bz.playCloseEncounters();  // Complete! (also gives servo time to close)
                ctx.servos[ctx.selected_scale]->off();  // Release servo (no holding torque)
            }

            // Manual stop (encoder press or web STOP)
            if ((pressed && !was_pressed_) || ctx.web_stop_dispense) {
                ctx.web_stop_dispense = false;
                float close_deg = servo_close(ctx.g_state, ctx.selected_scale);
                ctx.servos[ctx.selected_scale]->writeDegrees(close_deg);
                ctx.vibrators[ctx.selected_scale]->off();
                ctx.dispense_pid->SetMode(MANUAL);
                telem_end_run(dispensed_grams);
                ctx.net_lock();
                ctx.g_state.servo_angle = close_deg;
                ctx.g_state.vib_intensity = 0.0f;
                ctx.g_state.dispensing = false;
                ctx.net_unlock();
                sleep_ms(300);  // Give servo time to close
                ctx.servos[ctx.selected_scale]->off();  // Release servo
                state_ = DispenseState::Idle;
                option_ = 1;
                last_option_ = -1;
                resetLcdCache();
            }
            break;
        }

        case DispenseState::Done:
        {
            // Options: [Back] [Retry]
            if (delta != 0) {
                option_ += delta;
                if (option_ < 0) option_ = 0;
                if (option_ > 1) option_ = 1;
                last_encoder_pos_ = pos;
            }

            if (option_ != last_option_) {
                char opt_line[21];
                std::snprintf(opt_line, sizeof(opt_line), "  %s%s%s   %s%s%s  ",
                    option_ == 0 ? "[" : " ", "Back", option_ == 0 ? "]" : " ",
                    option_ == 1 ? "[" : " ", "Retry", option_ == 1 ? "]" : " ");
                ctx.lcd.setCursor(3, 0);
                ctx.lcd.print(opt_line);
                last_option_ = option_;
            }

            // Keep reading scale to show LIVE weight (may have overshot after closing)
            float live_dispensed = start_weight_ - ctx.scales[ctx.selected_scale]->read_weight();
            int display_live = (int)(live_dispensed + 0.5f);
            if (display_live < 0) display_live = 0;

            char line[21];
            if (ctx.target_grams != lcd_target_) {
                std::snprintf(line, sizeof(line), "Target: %d g    ", ctx.target_grams);
                ctx.lcd.setCursor(1, 0);
                ctx.lcd.print(line);
                lcd_target_ = ctx.target_grams;
            }
            if (display_live != lcd_value_) {
                std::snprintf(line, sizeof(line), "Dispensed: %d g ", display_live);
                ctx.lcd.setCursor(2, 0);
                ctx.lcd.print(line);
                lcd_value_ = display_live;
            }

            // Color on 7-segment based on accuracy
            ctx.sevenSeg->clear();
            if (live_dispensed > ctx.target_grams + 5) {
                ctx.sevenSeg->printNumber(display_live, 255, 0, 0);  // red - overshoot
            } else {
                ctx.sevenSeg->printNumber(display_live, 0, 255, 0);  // green - good
            }
            ctx.sevenSeg->show();

            // A web START while sitting on the Done screen: drop back to Idle,
            // which consumes web_start_dispense on the next iteration.
            if (ctx.web_start_dispense) {
                state_ = DispenseState::Idle;
                option_ = 1;
                last_option_ = -1;
                resetLcdCache();
            }

            if (pressed && !was_pressed_) {
                if (option_ == 0) {
                    next = ScreenId::Menu;
                } else {
                    // Retry - tare and restart
                    ctx.scales[ctx.selected_scale]->tare();
                    ctx.bz.playMarioCoin();  // Bling!
                    state_ = DispenseState::Idle;
                    option_ = 1;
                    last_option_ = -1;
                    resetLcdCache();
                }
            }
            break;
        }
        }

        was_pressed_ = pressed;
        sleep_ms(20);
        return next;
    }
};

// -------------------------------------------------------------- TestMenu ----

class TestMenuScreen : public Screen {
    int last_selected_ = -1;
public:
    void enter(UiContext& ctx) override {
        ctx.lcd.clear();
        ctx.lcd.setCursor(0, 2);
        ctx.lcd.print("Vibrators");
        ctx.lcd.setCursor(1, 2);
        ctx.lcd.print("Servos");
        ctx.lcd.setCursor(2, 2);
        ctx.lcd.print("Servo Zero");
        ctx.lcd.setCursor(3, 2);
        ctx.lcd.print("Back");
        indicatorArrow(ctx.lcd, 0, 0, 4);
        last_selected_ = 0;
    }

    ScreenId update(UiContext& ctx) override {
        int pos = ctx.enc.getPosition();
        int selected = ((pos % 4) + 4) % 4;

        if (selected != last_selected_) {
            indicatorArrow(ctx.lcd, selected, 0, 4);
            last_selected_ = selected;
        }

        if (ctx.enc.isPressed()) {
            if (selected == 0) return ScreenId::TestVibrator;
            if (selected == 1) return ScreenId::TestServo;
            if (selected == 2) return ScreenId::ServoCal;
            return ScreenId::Menu;
        }
        return ScreenId::TestMenu;
    }
};

// ---------------------------------------------------------- TestVibrator ----

class TestVibratorScreen : public Screen {
    int  selected_ = 0;      // 0, 1, 2 = vibrator index, 3 = Back
    int  intensity_ = 0;     // 0-100%
    int  last_encoder_pos_ = 0;
    bool was_pressed_ = false;
    bool adjusting_ = false;
public:
    void enter(UiContext& ctx) override {
        ctx.lcd.clear();
        ctx.lcd.setCursor(0, 0);
        ctx.lcd.print("Test Vibrators");
        ctx.lcd.setCursor(1, 0);
        ctx.lcd.print("Select: 1  2  3");

        last_encoder_pos_ = ctx.enc.getPosition();
        was_pressed_ = ctx.enc.isPressed();  // ignore carry-over press
        selected_ = 0;
        intensity_ = 0;
        adjusting_ = false;
        // Turn off all vibrators
        for (int i = 0; i < 3; i++) {
            ctx.vibrators[i]->off();
        }
    }

    ScreenId update(UiContext& ctx) override {
        int pos = ctx.enc.getPosition();
        int delta = pos - last_encoder_pos_;
        bool pressed = ctx.enc.isPressed();
        ScreenId next = ScreenId::TestVibrator;

        if (adjusting_) {
            // Encoder changes intensity
            if (delta != 0) {
                intensity_ += delta * 5;  // 5% steps
                if (intensity_ < 0) intensity_ = 0;
                if (intensity_ > 100) intensity_ = 100;
                last_encoder_pos_ = pos;
                ctx.vibrators[selected_]->setIntensity((float)intensity_ / 100.0f);
            }
            // Button exits intensity mode
            if (pressed && !was_pressed_) {
                adjusting_ = false;
                ctx.vibrators[selected_]->off();
                intensity_ = 0;
            }
        } else {
            // Encoder selects vibrator (0, 1, 2) or Back (3)
            if (delta != 0) {
                selected_ += delta;
                if (selected_ < 0) selected_ = 0;
                if (selected_ > 3) selected_ = 3;
                last_encoder_pos_ = pos;
            }
            if (pressed && !was_pressed_) {
                if (selected_ < 3) {
                    adjusting_ = true;
                    intensity_ = 0;
                } else {
                    next = ScreenId::Menu;
                }
            }
        }
        was_pressed_ = pressed;

        char line[21];
        if (adjusting_) {
            std::snprintf(line, sizeof(line), "Vibrator %d: %3d%%   ", selected_ + 1, intensity_);
            ctx.lcd.setCursor(2, 0);
            ctx.lcd.print(line);
            ctx.lcd.setCursor(3, 0);
            ctx.lcd.print("  Turn to adjust    ");
        } else {
            std::snprintf(line, sizeof(line), "%s1%s %s2%s %s3%s %sBack%s",
                selected_ == 0 ? "[" : " ", selected_ == 0 ? "]" : " ",
                selected_ == 1 ? "[" : " ", selected_ == 1 ? "]" : " ",
                selected_ == 2 ? "[" : " ", selected_ == 2 ? "]" : " ",
                selected_ == 3 ? "[" : " ", selected_ == 3 ? "]" : " ");
            ctx.lcd.setCursor(2, 0);
            ctx.lcd.print(line);
            ctx.lcd.setCursor(3, 0);
            ctx.lcd.print("Press to test       ");
        }

        // Show intensity on 7-segment
        ctx.sevenSeg->clear();
        if (adjusting_) {
            // Show intensity in color (blue->green->red)
            uint8_t r = intensity_ > 50 ? (intensity_ - 50) * 5 : 0;
            uint8_t g = intensity_ < 50 ? intensity_ * 5 : (100 - intensity_) * 5;
            uint8_t b = intensity_ < 50 ? (50 - intensity_) * 5 : 0;
            ctx.sevenSeg->printNumber(intensity_, r, g, b);
        } else {
            ctx.sevenSeg->printNumber(selected_ + 1, 0, 255, 0);
        }
        ctx.sevenSeg->show();

        sleep_ms(50);
        return next;
    }
};

// ------------------------------------------------------------- TestServo ----

class TestServoScreen : public Screen {
    int  selected_ = 0;      // 0, 1, 2 = servo index, 3 = Back
    int  angle_ = 0;         // 0-180 degrees (0 = closed)
    int  last_encoder_pos_ = 0;
    bool was_pressed_ = false;
    bool adjusting_ = false;
public:
    void enter(UiContext& ctx) override {
        ctx.lcd.clear();
        ctx.lcd.setCursor(0, 0);
        ctx.lcd.print("Test Servos");
        ctx.lcd.setCursor(1, 0);
        ctx.lcd.print("Select: 1  2  3");

        last_encoder_pos_ = ctx.enc.getPosition();
        was_pressed_ = ctx.enc.isPressed();  // ignore carry-over press
        selected_ = 0;
        angle_ = 0;
        adjusting_ = false;
        // Close all servos
        for (int i = 0; i < 3; i++) {
            ctx.servos[i]->writeDegrees(servo_close(ctx.g_state, i));
        }
    }

    ScreenId update(UiContext& ctx) override {
        int pos = ctx.enc.getPosition();
        int delta = pos - last_encoder_pos_;
        bool pressed = ctx.enc.isPressed();
        ScreenId next = ScreenId::TestServo;

        if (adjusting_) {
            // Encoder changes angle
            if (delta != 0) {
                angle_ += delta * 5;  // 5 degree steps
                if (angle_ < 0) angle_ = 0;
                if (angle_ > 180) angle_ = 180;
                last_encoder_pos_ = pos;
                ctx.servos[selected_]->writeDegrees((float)angle_);
            }
            // Button exits angle mode
            if (pressed && !was_pressed_) {
                adjusting_ = false;
                // Close, settle, release - off() also hands the shared PWM
                // slice back so a later vibrator-3 test doesn't hum at 333 Hz
                ctx.servos[selected_]->writeDegrees(servo_close(ctx.g_state, selected_));
                sleep_ms(300);
                ctx.servos[selected_]->off();
                angle_ = 0;
            }
        } else {
            // Encoder selects servo (0, 1, 2) or Back (3)
            if (delta != 0) {
                selected_ += delta;
                if (selected_ < 0) selected_ = 0;
                if (selected_ > 3) selected_ = 3;
                last_encoder_pos_ = pos;
            }
            if (pressed && !was_pressed_) {
                if (selected_ < 3) {
                    adjusting_ = true;
                    // Start at the servo's closed position
                    angle_ = (int)servo_close(ctx.g_state, selected_);
                    ctx.servos[selected_]->writeDegrees((float)angle_);
                } else {
                    // Leaving: release all servos (closed since enter())
                    for (int i = 0; i < 3; i++) {
                        ctx.servos[i]->off();
                    }
                    next = ScreenId::TestMenu;
                }
            }
        }
        was_pressed_ = pressed;

        char line[21];
        if (adjusting_) {
            std::snprintf(line, sizeof(line), "Servo %d: %3d deg   ", selected_ + 1, angle_);
            ctx.lcd.setCursor(2, 0);
            ctx.lcd.print(line);
            ctx.lcd.setCursor(3, 0);
            ctx.lcd.print("  Turn to adjust    ");
        } else {
            std::snprintf(line, sizeof(line), "%s1%s %s2%s %s3%s %sBack%s",
                selected_ == 0 ? "[" : " ", selected_ == 0 ? "]" : " ",
                selected_ == 1 ? "[" : " ", selected_ == 1 ? "]" : " ",
                selected_ == 2 ? "[" : " ", selected_ == 2 ? "]" : " ",
                selected_ == 3 ? "[" : " ", selected_ == 3 ? "]" : " ");
            ctx.lcd.setCursor(2, 0);
            ctx.lcd.print(line);
            ctx.lcd.setCursor(3, 0);
            ctx.lcd.print("Press to test       ");
        }

        // Show angle on 7-segment
        ctx.sevenSeg->clear();
        if (adjusting_) {
            // Color based on position: red at 0, green at 90, blue at 180
            uint8_t r = angle_ < 90 ? (90 - angle_) * 2 : 0;
            uint8_t g = 255 - abs(angle_ - 90) * 2;
            uint8_t b = angle_ > 90 ? (angle_ - 90) * 2 : 0;
            ctx.sevenSeg->printNumber(angle_, r, g, b);
        } else {
            ctx.sevenSeg->printNumber(selected_ + 1, 0, 255, 0);
        }
        ctx.sevenSeg->show();

        sleep_ms(50);
        return next;
    }
};

// -------------------------------------------------------------- ServoCal ----
// Calibrate each servo's "zero": jog the arm in 1-degree steps until grain
// just starts to flow, then save that angle. The zero becomes the PID's lower
// output limit and closing backs off SERVO_CLOSE_BACKOFF_DEG below it.

class ServoCalScreen : public Screen {
    enum class Phase { Select, Jog, Confirm };

    // Uncalibrated jog start: safely below the default flow region (85 deg),
    // so the gate never jump-opens onto the grain
    static constexpr int SERVO_CAL_START_UNCAL = 70;

    Phase phase_ = Phase::Select;
    int  selected_ = 0;      // 0-2 servo, 3 = Back
    int  angle_ = 0;         // current jog angle (degrees)
    int  option_ = 0;        // Confirm: 0=Save 1=Resume 2=Exit
    int  last_encoder_pos_ = 0;
    bool was_pressed_ = false;
    int  last_drawn_ = -1;   // change detector for the variable row

    static void closeAndRelease(UiContext& ctx, int i) {
        ctx.servos[i]->writeDegrees(servo_close(ctx.g_state, i));
        sleep_ms(300);
        ctx.servos[i]->off();
    }

    void drawSelect(UiContext& ctx) {
        char line[21];
        if (servo_zero_set(ctx.g_state, selected_ < 3 ? selected_ : 0) && selected_ < 3) {
            std::snprintf(line, sizeof(line), "Zero: %d deg        ",
                          (int)(ctx.g_state.servo_zero[selected_] + 0.5f));
        } else {
            std::snprintf(line, sizeof(line), "Zero: not set       ");
        }
        ctx.lcd.setCursor(1, 0);
        ctx.lcd.print(selected_ < 3 ? line : "                    ");
        std::snprintf(line, sizeof(line), "%s1%s %s2%s %s3%s %sBack%s      ",
            selected_ == 0 ? "[" : " ", selected_ == 0 ? "]" : " ",
            selected_ == 1 ? "[" : " ", selected_ == 1 ? "]" : " ",
            selected_ == 2 ? "[" : " ", selected_ == 2 ? "]" : " ",
            selected_ == 3 ? "[" : " ", selected_ == 3 ? "]" : " ");
        ctx.lcd.setCursor(2, 0);
        ctx.lcd.print(line);
        ctx.lcd.setCursor(3, 0);
        ctx.lcd.print("Press to adjust     ");
    }

public:
    void enter(UiContext& ctx) override {
        ctx.lcd.clear();
        ctx.lcd.setCursor(0, 0);
        ctx.lcd.print("Servo Zero");

        last_encoder_pos_ = ctx.enc.getPosition();
        was_pressed_ = ctx.enc.isPressed();  // ignore carry-over press
        phase_ = Phase::Select;
        selected_ = 0;
        last_drawn_ = -1;
        // Park all servos at their closed positions
        for (int i = 0; i < 3; i++) {
            ctx.servos[i]->writeDegrees(servo_close(ctx.g_state, i));
        }
        drawSelect(ctx);
    }

    ScreenId update(UiContext& ctx) override {
        int pos = ctx.enc.getPosition();
        int delta = pos - last_encoder_pos_;
        bool pressed = ctx.enc.isPressed();
        bool click = pressed && !was_pressed_;
        was_pressed_ = pressed;
        ScreenId next = ScreenId::ServoCal;
        char line[21];

        switch (phase_) {
        case Phase::Select:
            if (delta != 0) {
                selected_ += delta;
                if (selected_ < 0) selected_ = 0;
                if (selected_ > 3) selected_ = 3;
                last_encoder_pos_ = pos;
                drawSelect(ctx);
            }
            if (click) {
                if (selected_ < 3) {
                    angle_ = servo_zero_set(ctx.g_state, selected_)
                                 ? (int)servo_close(ctx.g_state, selected_)
                                 : SERVO_CAL_START_UNCAL;
                    ctx.servos[selected_]->writeDegrees((float)angle_);
                    phase_ = Phase::Jog;
                    last_drawn_ = -1;
                    ctx.lcd.setCursor(2, 0);
                    ctx.lcd.print("Turn: 1 deg steps   ");
                    ctx.lcd.setCursor(3, 0);
                    ctx.lcd.print("Press when flowing  ");
                } else {
                    for (int i = 0; i < 3; i++) {
                        ctx.servos[i]->off();
                    }
                    next = ScreenId::TestMenu;
                }
            }
            // 7-seg shows the highlighted servo number
            ctx.sevenSeg->clear();
            ctx.sevenSeg->printNumber(selected_ < 3 ? selected_ + 1 : 0, 0, 255, 0);
            ctx.sevenSeg->show();
            break;

        case Phase::Jog:
            if (delta != 0) {
                angle_ += delta;  // 1 degree per detent for fine calibration
                if (angle_ < 0) angle_ = 0;
                if (angle_ > 180) angle_ = 180;
                last_encoder_pos_ = pos;
                ctx.servos[selected_]->writeDegrees((float)angle_);
            }
            if (angle_ != last_drawn_) {
                if (servo_zero_set(ctx.g_state, selected_)) {
                    int rel = angle_ - (int)(ctx.g_state.servo_zero[selected_] + 0.5f);
                    std::snprintf(line, sizeof(line), "Angle: %3d  Rel:%+4d", angle_, rel);
                } else {
                    std::snprintf(line, sizeof(line), "Angle: %3d          ", angle_);
                }
                ctx.lcd.setCursor(1, 0);
                ctx.lcd.print(line);
                last_drawn_ = angle_;
            }
            if (click) {
                phase_ = Phase::Confirm;
                option_ = 0;
                last_drawn_ = -1;
                std::snprintf(line, sizeof(line), "Set zero at %d?     ", angle_);
                ctx.lcd.setCursor(2, 0);
                ctx.lcd.print(line);
            }
            // 7-seg mirrors the angle (red at 0, green at 90, blue at 180)
            {
                uint8_t r = angle_ < 90 ? (90 - angle_) * 2 : 0;
                uint8_t g = 255 - abs(angle_ - 90) * 2;
                uint8_t b = angle_ > 90 ? (angle_ - 90) * 2 : 0;
                ctx.sevenSeg->clear();
                ctx.sevenSeg->printNumber(angle_, r, g, b);
                ctx.sevenSeg->show();
            }
            break;

        case Phase::Confirm:
            if (delta != 0) {
                option_ += delta;
                if (option_ < 0) option_ = 0;
                if (option_ > 2) option_ = 2;
                last_encoder_pos_ = pos;
            }
            if (option_ != last_drawn_) {
                std::snprintf(line, sizeof(line), "%sSave%s %sResume%s %sExit%s ",
                    option_ == 0 ? "[" : " ", option_ == 0 ? "]" : " ",
                    option_ == 1 ? "[" : " ", option_ == 1 ? "]" : " ",
                    option_ == 2 ? "[" : " ", option_ == 2 ? "]" : " ");
                ctx.lcd.setCursor(3, 0);
                ctx.lcd.print(line);
                last_drawn_ = option_;
            }
            if (click) {
                if (option_ == 0) {
                    // Save: RAM under the lwIP lock (web reads it), flash via
                    // main loop (write stalls IRQs; main defers if dispensing)
                    ctx.net_lock();
                    ctx.g_state.servo_zero[selected_] = (float)angle_;
                    ctx.net_unlock();
                    ctx.servo_zero_save_request = true;
                    ctx.bz.playMarioCoin();
                    closeAndRelease(ctx, selected_);
                    phase_ = Phase::Select;
                } else if (option_ == 1) {
                    // Resume jogging (servo untouched)
                    phase_ = Phase::Jog;
                    ctx.lcd.setCursor(2, 0);
                    ctx.lcd.print("Turn: 1 deg steps   ");
                    ctx.lcd.setCursor(3, 0);
                    ctx.lcd.print("Press when flowing  ");
                } else {
                    // Exit without saving
                    closeAndRelease(ctx, selected_);
                    phase_ = Phase::Select;
                }
                if (phase_ == Phase::Select) {
                    ctx.lcd.setCursor(3, 0);
                    ctx.lcd.print("                    ");
                    drawSelect(ctx);
                }
                last_drawn_ = -1;
            }
            break;
        }

        sleep_ms(50);
        return next;
    }
};

// --------------------------------------------------------- ScreenManager ----

static MenuScreen           s_menu;
static SelectScaleScreen    s_selectScale;
static Calibrate1Screen     s_calibrate1;
static Calibrate2Screen     s_calibrate2;
static SetTargetScreen      s_setTarget;
static SetTargetDigitScreen s_setTargetDigit;
static WeighScreen          s_weigh;
static DispenseScreen       s_dispense;
static TestMenuScreen       s_testMenu;
static TestVibratorScreen   s_testVibrator;
static TestServoScreen      s_testServo;
static ServoCalScreen       s_servoCal;

static Screen* screenFor(ScreenId id) {
    switch (id) {
    case ScreenId::Menu:           return &s_menu;
    case ScreenId::SelectScale:    return &s_selectScale;
    case ScreenId::Calibrate1:     return &s_calibrate1;
    case ScreenId::Calibrate2:     return &s_calibrate2;
    case ScreenId::SetTarget:      return &s_setTarget;
    case ScreenId::SetTargetDigit: return &s_setTargetDigit;
    case ScreenId::Weigh:          return &s_weigh;
    case ScreenId::Dispense:       return &s_dispense;
    case ScreenId::TestMenu:       return &s_testMenu;
    case ScreenId::TestVibrator:   return &s_testVibrator;
    case ScreenId::TestServo:      return &s_testServo;
    case ScreenId::ServoCal:       return &s_servoCal;
    }
    return &s_menu;
}

void ScreenManager::init(UiContext& ctx, ScreenId start) {
    current_ = start;
    screenFor(current_)->enter(ctx);
}

void ScreenManager::tick(UiContext& ctx) {
    ScreenId next = screenFor(current_)->update(ctx);
    if (next != current_) {
        current_ = next;
        screenFor(current_)->enter(ctx);
    }
}

void ScreenManager::goTo(UiContext& ctx, ScreenId id) {
    current_ = id;
    screenFor(current_)->enter(ctx);
}

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include <cstdio>
#include <cstdlib>

#include "Buzzer.hpp"
#include "Rotary_Button.hpp"
#include "Lcd1602I2C.hpp"
#include "hx711.hpp"
#include "config_store.hpp"
#include "Ws2812.hpp"
#include "SevenSeg.hpp"
#include "Servo.hpp"
#include "Vibrator.hpp"
#include "SharedSlice.hpp"
#include "PID.hpp"
#include "telemetry.hpp"

#include "wifi_config.h"
#include "dispenser_state.h"
#include "web_server.h"
#include "dhserver.h"

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

// servo1 (GP7) and vib3 (GP22) both live on PWM slice 3, which can only hold one
// frequency. This coordinator runs the slice at the servo's 333 Hz whenever servo1
// is active and hands it back to the vibrator's 20 kHz otherwise (see main()).
SharedSlice slice3_pwm;

// Pointers - initialized in main() to avoid PIO conflict with encoder
Ws2812* sevenSegStrip = nullptr;
SevenSeg* sevenSeg = nullptr;

ScaleConfig sc;

// Shared state for web interface
DispenserState g_state;

// True once cyw43_arch_init() has succeeded - lwIP callbacks may then run in the
// background IRQ context, and shared state must be guarded with the lwIP lock.
static bool net_stack_up = false;
static inline void net_lock()   { if (net_stack_up) cyw43_arch_lwip_begin(); }
static inline void net_unlock() { if (net_stack_up) cyw43_arch_lwip_end(); }

// PID tuning parameters (mutable for web-based tuning)
static double Kp = 1.5, Ki = 0.08, Kd = 0.8;
static PID* dispense_pid = nullptr;
static bool pid_save_pending = false;  // Deferred flash save (never write mid-dispense)

static void save_pid_gains() {
    PidConfig pc;
    pc.kp = (float)Kp;
    pc.ki = (float)Ki;
    pc.kd = (float)Kd;
    save_pid_config(pc);  // stalls IRQs ~100 ms - only call while not dispensing
}

// --- Access-point fallback ---------------------------------------------------
// When the router is unreachable, the Pico broadcasts its own network so a phone
// can join it directly and use the web app at http://192.168.4.1.
static dhcp_entry_t dhcp_entries[] = {
    { {0}, {192, 168, 4, 16}, {255, 255, 255, 0}, 24 * 3600 },
    { {0}, {192, 168, 4, 17}, {255, 255, 255, 0}, 24 * 3600 },
    { {0}, {192, 168, 4, 18}, {255, 255, 255, 0}, 24 * 3600 },
    { {0}, {192, 168, 4, 19}, {255, 255, 255, 0}, 24 * 3600 },
};

static dhcp_config_t dhcp_cfg = {
    {192, 168, 4, 1},                // server address
    67,                              // port
    {192, 168, 4, 1},                // dns (points at us; we run no DNS - harmless)
    "korn",                          // domain suffix
    sizeof(dhcp_entries) / sizeof(dhcp_entries[0]),
    dhcp_entries
};

static bool start_ap_mode() {
    cyw43_arch_disable_sta_mode();
    // The AP netif is auto-configured to 192.168.4.1/24 by the cyw43 driver
    cyw43_arch_enable_ap_mode(WIFI_AP_SSID, WIFI_AP_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);

    cyw43_arch_lwip_begin();
    err_t e = dhserv_init(&dhcp_cfg);
    cyw43_arch_lwip_end();
    if (e != ERR_OK) {
        printf("[wifi] AP dhcp server failed: %d\n", (int)e);
        return false;
    }
    printf("[wifi] AP mode: %s at 192.168.4.1\n", WIFI_AP_SSID);
    return true;
}

int main()
{
    stdio_init_all();

    // Load persisted PID gains (if ever saved) before the controller is created
    {
        PidConfig pc;
        if (load_pid_config(pc)) {
            Kp = pc.kp;
            Ki = pc.ki;
            Kd = pc.kd;
        }
    }

    // Link servo1 + vib3 on shared PWM slice 3 before any servo/vibrator output, so
    // the slice frequency is arbitrated from the very first startup close below.
    servo1.attachShared(&slice3_pwm);
    vib3.attachShared(&slice3_pwm);

    // Close all servos at startup, then release (no holding torque)
    for (int i = 0; i < 3; i++) {
        servos[i]->writeDegrees(0.0f);  // Closed position
    }
    sleep_ms(500);  // Give servos time to reach position
    for (int i = 0; i < 3; i++) {
        servos[i]->off();  // Release - no holding torque
    }

    // Give USB serial a moment to connect (non-blocking)
    sleep_ms(1000);
    printf("Ready.\r\r\n");

    lcd.init(PICO_DEFAULT_I2C_SDA_PIN, PICO_DEFAULT_I2C_SCL_PIN, 100000);

    // --- WiFi initialization ---
    if (cyw43_arch_init()) {
        printf("[wifi] init failed\n");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi init failed!");
        sleep_ms(2000);
    } else {
        net_stack_up = true;

        // Holding the encoder button at boot skips STA and forces AP mode
        bool force_ap = enc.isPressed();

        int wifi_err = -1;
        if (!force_ap) {
            cyw43_arch_enable_sta_mode();
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Connecting WiFi...");
            lcd.setCursor(1, 0);
            lcd.print(WIFI_SSID);
            printf("[wifi] connecting to %s...\n", WIFI_SSID);

            for (int attempt = 1; attempt <= 3 && wifi_err != 0; attempt++) {
                printf("[wifi] attempt %d/3...\n", attempt);
                lcd.setCursor(2, 0);
                char att_line[21];
                std::snprintf(att_line, sizeof(att_line), "Attempt %d/3...      ", attempt);
                lcd.print(att_line);

                wifi_err = cyw43_arch_wifi_connect_timeout_ms(
                    WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000);

                if (wifi_err) {
                    printf("[wifi] attempt %d failed: %d\n", attempt, wifi_err);
                }
            }
        }

        if (wifi_err == 0) {
            const ip4_addr_t* ip = netif_ip4_addr(netif_default);
            char ip_str[20];
            snprintf(ip_str, sizeof(ip_str), "%s", ip4addr_ntoa(ip));
            printf("[wifi] connected: %s\n", ip_str);

            lcd.setCursor(2, 0);
            lcd.print("IP:");
            lcd.setCursor(3, 0);
            lcd.print(ip_str);

            // Start web server
            web_server_init(&g_state, WEB_SERVER_PORT);
            sleep_ms(2000);
        } else {
            // STA failed (or AP forced) - broadcast our own network instead
            if (!force_ap) printf("[wifi] all attempts failed: %d\n", wifi_err);
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(force_ap ? "AP mode (forced)" : "WiFi failed - AP up");

            if (start_ap_mode()) {
                g_state.ap_mode = true;
                lcd.setCursor(1, 0);
                lcd.print("AP: " WIFI_AP_SSID);
                lcd.setCursor(2, 0);
                lcd.print("Pass: " WIFI_AP_PASSWORD);
                lcd.setCursor(3, 0);
                lcd.print("http://192.168.4.1");

                web_server_init(&g_state, WEB_SERVER_PORT);
                sleep_ms(4000);
            } else {
                lcd.setCursor(3, 0);
                lcd.print("Continuing offline..");
                sleep_ms(3000);
            }
        }
    }

    bz.playMacStartup(); // Mac-like startup chime

    // Initialize 7-segment display after encoder to avoid PIO conflict
    sevenSegStrip = new Ws2812(SEVENSEG_PIN, SEVENSEG_LEDS);
    sevenSeg = new SevenSeg(*sevenSegStrip, LAYOUT_6DIGITS);

    // --- 7-segment startup animation: random segments sweep left to right ---
    {
        const uint8_t ORANGE_R = 255, ORANGE_G = 80, ORANGE_B = 0;
        const int SWEEP_DELAY_MS = 40;  // Speed of sweep
        const int HOLD_MS = 300;        // Hold at end before clearing

        // Seed random with time
        srand(to_ms_since_boot(get_absolute_time()));

        // Sweep from left to right, lighting random segments
        for (int digit = 0; digit < 6; digit++) {
            // Generate random segment mask (at least 2 segments lit)
            uint8_t mask = 0;
            int lit_count = 0;
            while (lit_count < 2) {
                mask = rand() % 128;  // 7 bits for 7 segments
                lit_count = 0;
                for (int b = 0; b < 7; b++) {
                    if (mask & (1 << b)) lit_count++;
                }
            }

            sevenSeg->setDigitMask(digit, mask, ORANGE_R, ORANGE_G, ORANGE_B);
            sevenSeg->show();
            sleep_ms(SWEEP_DELAY_MS);
        }

        // Brief hold showing all random segments
        sleep_ms(HOLD_MS);

        // Quick fade out sweep right to left
        for (int digit = 5; digit >= 0; digit--) {
            sevenSeg->setDigitMask(digit, 0, 0, 0, 0);
            sevenSeg->show();
            sleep_ms(SWEEP_DELAY_MS / 2);
        }
    }

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
            sleep_ms(2500);
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
            sleep_ms(2500);
        }
    }
    else
    {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("     No valid");
        lcd.setCursor(1, 0);
        lcd.print("  config in flash   ");
        sleep_ms(2500);
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
        TestMenu,
        TestVibrator,
        TestServo
    };

    ScreenId current = isConfigured ? ScreenId::Menu : ScreenId::SelectScale;
    ScreenId last_screen = static_cast<ScreenId>(-1);
    ScreenId after_select = ScreenId::Calibrate1;  // Where to go after SelectScale
    ScreenId after_target = ScreenId::Weigh;       // Where to go after SetTargetDigit
    int last_selected = -1;
    int target_grams = 100;  // default target weight
    int selected_scale = 0;  // 0=Scale1, 1=Scale2, 2=Scale3
    bool web_start_dispense = false;  // Flag: web UI requested dispense start
    bool web_stop_dispense = false;   // Flag: web UI requested dispense stop (consumed by Running state)
    bool web_active = false;  // When true, hardware input is disabled (web controls only)

    // Lambda to draw menu arrow indicator (supports up to 4 rows)
    auto indicatorArrow = [&](int lineNumber, int startRow = 0, int count = 3) {
        for (int i = 0; i < count; i++) {
            lcd.setCursor(startRow + i, 0);
            lcd.writeChar(i == lineNumber ? '>' : ' ');
        }
    };

    // Timestamp for periodic background weight reads
    absolute_time_t last_bg_weight_time = get_absolute_time();
    int bg_weight_cycle = 0;  // cycles through non-selected scales

    while (true)
    {
        // --- Web state sync: update g_state from local variables ---
        // Scale reads happen OUTSIDE the lwIP lock (they bit-bang the HX711);
        // only the g_state assignments are guarded so /api/status snapshots
        // taken in the lwIP IRQ context can't tear across fields.
        bool have_weights = false;
        float w_sel = 0.0f, w_other = 0.0f;
        int other = (selected_scale + 1 + bg_weight_cycle) % 3;
        if (absolute_time_diff_us(last_bg_weight_time, get_absolute_time()) > 250000) {
            w_sel = scales[selected_scale]->read_weight();
            if (other != selected_scale) {
                w_other = scales[other]->read_weight();
            }
            have_weights = true;
            bg_weight_cycle = (bg_weight_cycle + 1) % 2;
            last_bg_weight_time = get_absolute_time();
        }

        net_lock();
        g_state.selected_scale = selected_scale;
        g_state.target_grams = target_grams;
        if (have_weights) {
            g_state.weights[selected_scale] = w_sel;
            if (other != selected_scale) g_state.weights[other] = w_other;
        }
        g_state.pid_kp = (float)Kp;
        g_state.pid_ki = (float)Ki;
        g_state.pid_kd = (float)Kd;
        for (int i = 0; i < 3; i++) {
            g_state.scale_calibrated[i] =
                (scales[i]->get_offset() != 0 || scales[i]->get_scale() != 1.0f);
        }
        net_unlock();

        // --- Web command dispatch ---
        // Copy the command AND its payload fields atomically, then dispatch from
        // the locals (a second web request could otherwise overwrite cmd_* mid-use).
        WebCommand cmd;
        int   cmd_target, cmd_scale, cmd_cal_weight;
        float cmd_servo_angle, cmd_vib_intensity;
        float cmd_pid_kp, cmd_pid_ki, cmd_pid_kd;
        net_lock();
        cmd = g_state.pending_command;
        g_state.pending_command = WebCommand::None;
        cmd_target        = g_state.cmd_target;
        cmd_scale         = g_state.cmd_scale;
        cmd_cal_weight    = g_state.cmd_cal_weight;
        cmd_servo_angle   = g_state.cmd_servo_angle;
        cmd_vib_intensity = g_state.cmd_vib_intensity;
        cmd_pid_kp        = g_state.cmd_pid_kp;
        cmd_pid_ki        = g_state.cmd_pid_ki;
        cmd_pid_kd        = g_state.cmd_pid_kd;
        net_unlock();
        if (cmd != WebCommand::None) {
            web_active = true;  // Web is in control, disable hardware input

            switch (cmd) {
            case WebCommand::Tare:
                scales[selected_scale]->tare();
                bz.playMarioCoin();
                break;

            case WebCommand::SetTarget:
                target_grams = cmd_target;
                if (target_grams < 1) target_grams = 1;
                if (target_grams > 9999) target_grams = 9999;
                break;

            case WebCommand::SelectScale:
                if (cmd_scale >= 0 && cmd_scale <= 2) {
                    selected_scale = cmd_scale;
                }
                break;

            case WebCommand::StartDispense:
                // Switch to dispense screen and set flag to auto-start
                if (!g_state.dispensing) {
                    current = ScreenId::Dispense;
                    last_screen = static_cast<ScreenId>(-1);  // force redraw
                    web_start_dispense = true;
                    g_state.dispense_done = false;
                }
                break;

            case WebCommand::StopDispense:
                // Route through the Dispense screen's Running state so the state
                // machine, telemetry and web state all stop consistently. (Closing
                // the servo here alone is not enough - Running would re-open it.)
                if (g_state.dispensing) {
                    web_stop_dispense = true;
                }
                break;

            case WebCommand::TestServo:
                servos[selected_scale]->writeDegrees(cmd_servo_angle);
                break;

            case WebCommand::TestVibrator:
                vibrators[selected_scale]->setIntensity(cmd_vib_intensity);
                break;

            case WebCommand::TestStop:
                for (int i = 0; i < 3; i++) {
                    servos[i]->writeDegrees(0.0f);
                    vibrators[i]->off();
                }
                sleep_ms(300);
                for (int i = 0; i < 3; i++) {
                    servos[i]->off();
                }
                break;

            case WebCommand::SetPID:
                Kp = (double)cmd_pid_kp;
                Ki = (double)cmd_pid_ki;
                Kd = (double)cmd_pid_kd;
                if (dispense_pid) {
                    dispense_pid->SetTunings(Kp, Ki, Kd);
                }
                // Persist - but never write flash mid-dispense (IRQ stall)
                if (!g_state.dispensing) {
                    save_pid_gains();
                } else {
                    pid_save_pending = true;
                }
                break;

            case WebCommand::Calibrate:
                if (cmd_cal_weight > 0) {
                    scales[selected_scale]->calibrate_scale(
                        (float)cmd_cal_weight, 10);
                    sc.entries[selected_scale].offset_counts =
                        scales[selected_scale]->get_offset();
                    sc.entries[selected_scale].count_per_g =
                        scales[selected_scale]->get_scale();
                    save_scale_config(sc);
                    bz.playMarioCoin();
                }
                break;

            default:
                break;
            }
        }

        // Flush a PID save that was deferred because a dispense was running
        if (pid_save_pending && !g_state.dispensing) {
            pid_save_pending = false;
            save_pid_gains();
        }

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
                lcd.print("Remove all weight   ");
                lcd.setCursor(2, 0);
                lcd.print("then select:        ");
                // Row 3 options are drawn in the logic section
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

            case ScreenId::TestMenu:
                lcd.setCursor(0, 0);
                lcd.print("Test Menu");
                lcd.setCursor(1, 2);
                lcd.print("Vibrators");
                lcd.setCursor(2, 2);
                lcd.print("Servos");
                lcd.setCursor(3, 2);
                lcd.print("Back");
                indicatorArrow(0, 1, 3);
                last_selected = 0;
                break;

            case ScreenId::TestVibrator:
                lcd.setCursor(0, 0);
                lcd.print("Test Vibrators");
                lcd.setCursor(1, 0);
                lcd.print("Select: 1  2  3");
                break;

            case ScreenId::TestServo:
                lcd.setCursor(0, 0);
                lcd.print("Test Servos");
                lcd.setCursor(1, 0);
                lcd.print("Select: 1  2  3");
                break;
            }
            last_screen = current;
        }

        // When web is active, show status on LCD but skip all hardware input.
        // EXCEPTION: the Dispense screen must keep running - it owns the PID
        // loop, so a web-started dispense would otherwise never actually run.
        if (web_active && current != ScreenId::Dispense) {
            static bool web_lcd_drawn = false;

            // Encoder press (while idle) hands control back to the local UI
            if (enc.isPressed() && !g_state.dispensing) {
                web_active = false;
                web_lcd_drawn = false;
                current = ScreenId::Menu;
                last_screen = static_cast<ScreenId>(-1);  // force redraw
                continue;
            }

            if (!web_lcd_drawn) {
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("  Web Control Active");
                web_lcd_drawn = true;
            }
            // Show live info on LCD
            char wline[21];
            float wg = scales[selected_scale]->read_weight();
            std::snprintf(wline, sizeof(wline), "Scale %d: %d g      ", selected_scale + 1, (int)(wg + 0.5f));
            lcd.setCursor(1, 0);
            lcd.print(wline);
            std::snprintf(wline, sizeof(wline), "Target: %d g        ", target_grams);
            lcd.setCursor(2, 0);
            lcd.print(wline);
            // WiFi signal strength
            int32_t rssi = -100;
            cyw43_wifi_get_rssi(&cyw43_state, &rssi);
            if (g_state.dispensing) {
                std::snprintf(wline, sizeof(wline), "Dispensing: %d g    ", (int)(g_state.dispensed_grams + 0.5f));
            } else {
                std::snprintf(wline, sizeof(wline), "WiFi: %ld dBm %s", (long)rssi,
                    rssi > -50 ? "Great" : rssi > -65 ? "Good" : rssi > -75 ? "Fair" : "Weak");
            }
            lcd.setCursor(3, 0);
            lcd.print(wline);

            // Update 7-segment with weight
            int display_w = (int)(wg + 0.5f);
            sevenSeg->clear();
            sevenSeg->printNumber(display_w < 0 ? 0 : display_w, 0, 128, 255);
            sevenSeg->show();

            sleep_ms(100);
            continue;  // Skip the hardware input switch below
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
                    // Test - go to test submenu
                    current = ScreenId::TestMenu;
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
            // Option selection: Tare or Back
            static int cal1_option = 0;  // 0=Tare, 1=Back
            static int last_cal1_option = -1;
            static int last_encoder_pos_cal1 = 0;
            static bool first_entry_cal1 = true;
            static bool was_pressed_cal1 = false;

            if (first_entry_cal1) {
                last_encoder_pos_cal1 = enc.getPosition();
                first_entry_cal1 = false;
                was_pressed_cal1 = enc.isPressed();
                cal1_option = 0;
                last_cal1_option = -1;
            }

            int pos = enc.getPosition();
            int delta = pos - last_encoder_pos_cal1;
            if (delta != 0) {
                cal1_option += delta;
                if (cal1_option < 0) cal1_option = 0;
                if (cal1_option > 1) cal1_option = 1;
                last_encoder_pos_cal1 = pos;
            }

            // Update option display
            if (cal1_option != last_cal1_option) {
                char opt_line[21];
                std::snprintf(opt_line, sizeof(opt_line), "   %s%s%s%s%s%s",
                    cal1_option == 0 ? "[" : " ", "Tare", cal1_option == 0 ? "]" : " ",
                    cal1_option == 1 ? "[" : " ", "Back", cal1_option == 1 ? "]" : " ");
                lcd.setCursor(3, 0);
                lcd.print(opt_line);
                last_cal1_option = cal1_option;
            }

            bool pressed = enc.isPressed();
            if (pressed && !was_pressed_cal1) {
                if (cal1_option == 0) {
                    // Tare and continue
                    scales[selected_scale]->tare();
                    bz.playMarioCoin();  // Bling!
                    first_entry_cal1 = true;
                    current = ScreenId::Calibrate2;
                } else {
                    // Back to menu
                    first_entry_cal1 = true;
                    current = ScreenId::Menu;
                }
            }
            was_pressed_cal1 = pressed;

            sleep_ms(50);
            break;
        }

        case ScreenId::Calibrate2:
        {
            // Digit entry: 4 digits + OK + Back buttons
            // Navigation mode: encoder moves cursor, button enters edit/confirms
            // Edit mode: encoder changes digit value, button exits edit
            static int digits[4] = {1, 0, 5, 6};  // Default 1056g
            static int cursor_pos = 0;  // 0-3 = digits, 4 = OK, 5 = Back
            static int last_encoder_pos = 0;
            static bool first_entry = true;
            static bool was_pressed = false;
            static bool edit_mode = false;

            if (first_entry) {
                last_encoder_pos = enc.getPosition();
                first_entry = false;
                was_pressed = enc.isPressed();
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
                    if (cursor_pos > 5) cursor_pos = 5;  // 0-3=digits, 4=OK, 5=Back
                    last_encoder_pos = pos;
                }
                // Button enters edit mode or confirms OK/Back
                if (pressed && !was_pressed) {
                    if (cursor_pos < 4) {
                        // Enter edit mode for this digit
                        edit_mode = true;
                    } else if (cursor_pos == 4) {
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
                    } else {
                        // Back pressed - return to menu without saving
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
                std::snprintf(line1, sizeof(line1), "%s%s%s%sg", d[0], d[1], d[2], d[3]);
            } else {
                // Format: "1 0 5 6 g [OK][Back]" - compact to fit
                std::snprintf(line1, sizeof(line1), "%d %d %d %d g %s%s",
                             digits[0], digits[1], digits[2], digits[3],
                             cursor_pos == 4 ? "[OK]" : " OK ",
                             cursor_pos == 5 ? "[Back]" : " Back ");
            }
            lcd.setCursor(2, 0);
            lcd.print(line1);

            // Cursor line (only in navigation mode)
            char line2[21] = "                    ";
            if (!edit_mode && cursor_pos < 4) {
                // Cursor under digits: positions 0, 2, 4, 6
                int cursor_x = cursor_pos * 2;
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
                // Format: "[Tare][Target][Back]" - no spacing between options
                char opt_line[21];
                std::snprintf(opt_line, sizeof(opt_line), "%s%s%s%s%s%s%s%s%s",
                    weigh_option == 0 ? "[" : " ", "Tare", weigh_option == 0 ? "]" : " ",
                    weigh_option == 1 ? "[" : " ", "Target", weigh_option == 1 ? "]" : " ",
                    weigh_option == 2 ? "[" : " ", "Back", weigh_option == 2 ? "]" : " ");
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
                    bz.playMarioCoin();  // Bling!
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

            // PID variables - values from working Arduino code
            static double pid_input = 0.0;
            static double pid_output = 0.0;
            static double pid_setpoint = 0.0;
            // Kp, Ki, Kd and dispense_pid are file-scope statics
            static const float SERVO_MIN_OPEN = 85.0f;  // where hole starts opening
            static const float SERVO_OPEN = 170.0f;    // fully open

            // Track weight decrease
            static float start_weight = 0.0f;  // Weight when dispense started
            static float final_dispensed = 0.0f;  // Final amount dispensed (for Done screen)
            static uint32_t dispense_start_ms = 0;  // For telemetry timestamps

            if (first_entry_disp) {
                last_encoder_pos_disp = enc.getPosition();
                first_entry_disp = false;
                was_pressed_disp = enc.isPressed();  // Capture current state to ignore carry-over press
                disp_state = DispenseState::Idle;
                disp_option = 1;  // Default to Start
                last_disp_option = -1;

                // Create PID if needed
                if (dispense_pid == nullptr) {
                    dispense_pid = new PID(&pid_input, &pid_output, &pid_setpoint,
                                          Kp, Ki, Kd, DIRECT);
                    dispense_pid->SetOutputLimits(SERVO_MIN_OPEN, SERVO_OPEN);  // Output is servo angle in opening range only
                    dispense_pid->SetSampleTime(50);
                }
            }

            // Read current weight from scale (3-sample average for noise reduction)
            float current_grams = scales[selected_scale]->read_weight(3);
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

                // Handle button or web-initiated start
                bool do_start = web_start_dispense;
                web_start_dispense = false;

                if (pressed && !was_pressed_disp) {
                    if (disp_option == 0) {
                        // Set target - go to digit entry, come back here
                        first_entry_disp = true;
                        after_target = ScreenId::Dispense;
                        current = ScreenId::SetTargetDigit;
                    } else if (disp_option == 1) {
                        do_start = true;
                    } else {
                        // Back to menu
                        first_entry_disp = true;
                        current = ScreenId::Menu;
                    }
                }
                if (do_start) {
                    web_stop_dispense = false;  // Clear any stale stop request
                    // Start dispensing - first tare the scale
                    lcd.setCursor(2, 0);
                    lcd.print("Taring...           ");
                    scales[selected_scale]->tare();
                    bz.playMarioCoin();
                    sleep_ms(300);

                    start_weight = scales[selected_scale]->read_weight(3);
                    pid_setpoint = (double)target_grams;
                    pid_input = 0.0;
                    dispense_pid->SetMode(AUTOMATIC);

                    // Start telemetry capture (under lwIP lock so a CSV download
                    // in flight can't observe the buffer reset mid-row)
                    dispense_start_ms = to_ms_since_boot(get_absolute_time());
                    net_lock();
                    telem_begin_run((uint8_t)selected_scale, (uint16_t)target_grams,
                                    (float)Kp, (float)Ki, (float)Kd);
                    net_unlock();

                    disp_state = DispenseState::Running;
                    disp_option = 0;
                    last_disp_option = -1;
                    net_lock();
                    g_state.dispensing = true;
                    g_state.dispense_done = false;
                    g_state.dispensed_grams = 0;
                    net_unlock();
                    lcd.setCursor(3, 0);
                    lcd.print("   [Stop]           ");
                }
                break;
            }

            case DispenseState::Running:
            {
                // PID control - input is dispensed amount, setpoint is target
                // PID output is servo angle directly (like Arduino)
                pid_input = (double)dispensed_grams;
                bool pid_computed = dispense_pid->Compute();

                // Use PID output directly as servo angle
                float servo_angle = (float)pid_output;

                // Vibrator: only activate when <=30g remaining
                float remaining = (float)target_grams - dispensed_grams;
                if (remaining <= 30.0f) {
                    vibrators[selected_scale]->setIntensity(0.6f);
                } else {
                    vibrators[selected_scale]->off();
                }

                servos[selected_scale]->writeDegrees(servo_angle);

                // Log a telemetry sample per actual PID computation (20 Hz)
                if (pid_computed) {
                    TelemetrySample ts;
                    ts.t_ms      = to_ms_since_boot(get_absolute_time()) - dispense_start_ms;
                    ts.setpoint  = (float)pid_setpoint;
                    ts.dispensed = dispensed_grams;
                    ts.weight    = current_grams;
                    ts.servo     = servo_angle;
                    ts.p         = (float)dispense_pid->GetLastP();
                    ts.i         = (float)dispense_pid->GetLastI();
                    ts.d         = (float)dispense_pid->GetLastD();
                    ts.vib       = (remaining <= 30.0f) ? 0.6f : 0.0f;
                    telem_append(ts);
                }

                // Sync web state
                net_lock();
                g_state.dispensed_grams = dispensed_grams;
                g_state.servo_angle = servo_angle;
                g_state.vib_intensity = (remaining <= 30.0f) ? 0.6f : 0.0f;
                net_unlock();

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
                if (dispensed_grams >= (float)target_grams) {
                    // Done!
                    final_dispensed = dispensed_grams;  // Remember final amount
                    servos[selected_scale]->writeDegrees(0.0f);  // Close servo
                    vibrators[selected_scale]->off();
                    dispense_pid->SetMode(MANUAL);
                    telem_end_run(final_dispensed);
                    disp_state = DispenseState::Done;
                    disp_option = 0;
                    last_disp_option = -1;
                    net_lock();
                    g_state.dispensing = false;
                    g_state.dispense_done = true;
                    g_state.dispensed_grams = final_dispensed;
                    g_state.servo_angle = 0.0f;
                    g_state.vib_intensity = 0.0f;
                    net_unlock();
                    bz.playCloseEncounters();  // Dispense complete! (also gives servo time to close)
                    servos[selected_scale]->off();  // Release servo (no holding torque)
                }

                // Manual stop (encoder press or web STOP)
                if ((pressed && !was_pressed_disp) || web_stop_dispense) {
                    web_stop_dispense = false;
                    servos[selected_scale]->writeDegrees(0.0f);  // Close servo
                    vibrators[selected_scale]->off();
                    dispense_pid->SetMode(MANUAL);
                    telem_end_run(dispensed_grams);
                    net_lock();
                    g_state.servo_angle = 0.0f;
                    g_state.vib_intensity = 0.0f;
                    g_state.dispensing = false;
                    net_unlock();
                    sleep_ms(300);  // Give servo time to close
                    servos[selected_scale]->off();  // Release servo
                    disp_state = DispenseState::Idle;
                    disp_option = 1;
                    last_disp_option = -1;
                }
                break;
            }

            case DispenseState::Done:
            {
                // Options: [Back] [Retry]
                if (delta != 0) {
                    disp_option += delta;
                    if (disp_option < 0) disp_option = 0;
                    if (disp_option > 1) disp_option = 1;
                    last_encoder_pos_disp = pos;
                }

                if (disp_option != last_disp_option) {
                    char opt_line[21];
                    std::snprintf(opt_line, sizeof(opt_line), "  %s%s%s   %s%s%s  ",
                        disp_option == 0 ? "[" : " ", "Back", disp_option == 0 ? "]" : " ",
                        disp_option == 1 ? "[" : " ", "Retry", disp_option == 1 ? "]" : " ");
                    lcd.setCursor(3, 0);
                    lcd.print(opt_line);
                    last_disp_option = disp_option;
                }

                // Keep reading scale to show LIVE weight (may have overshot after closing)
                float live_dispensed = start_weight - scales[selected_scale]->read_weight(3);
                int display_live = (int)(live_dispensed + 0.5f);
                if (display_live < 0) display_live = 0;

                // Show result with live scale reading
                char line[21];
                std::snprintf(line, sizeof(line), "Target: %d g    ", target_grams);
                lcd.setCursor(1, 0);
                lcd.print(line);
                std::snprintf(line, sizeof(line), "Dispensed: %d g ", display_live);
                lcd.setCursor(2, 0);
                lcd.print(line);

                // Color on 7-segment based on accuracy
                sevenSeg->clear();
                if (live_dispensed > target_grams + 5) {
                    sevenSeg->printNumber(display_live, 255, 0, 0);  // red - overshoot
                } else {
                    sevenSeg->printNumber(display_live, 0, 255, 0);  // green - good
                }
                sevenSeg->show();

                // A web START while sitting on the Done screen: drop back to Idle,
                // which consumes web_start_dispense on the next iteration.
                if (web_start_dispense) {
                    disp_state = DispenseState::Idle;
                    disp_option = 1;
                    last_disp_option = -1;
                }

                if (pressed && !was_pressed_disp) {
                    if (disp_option == 0) {
                        // Back - return to menu
                        first_entry_disp = true;
                        current = ScreenId::Menu;
                    } else {
                        // Retry - tare and restart
                        scales[selected_scale]->tare();
                        bz.playMarioCoin();  // Bling!
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

        case ScreenId::TestMenu:
        {
            // Test submenu: Vibrators, Servos, Back
            int pos = enc.getPosition();
            int selected = ((pos % 3) + 3) % 3;

            if (selected != last_selected)
            {
                indicatorArrow(selected, 1, 3);
                last_selected = selected;
            }

            if (enc.isPressed())
            {
                if (selected == 0) {
                    current = ScreenId::TestVibrator;
                } else if (selected == 1) {
                    current = ScreenId::TestServo;
                } else {
                    current = ScreenId::Menu;
                }
            }
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
                was_pressed_test = enc.isPressed();  // Capture current state to ignore carry-over press
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

        case ScreenId::TestServo:
        {
            // Servo test screen - select servo and control angle with encoder
            static int test_servo_selected = 0;  // 0, 1, 2 = servo index
            static int test_servo_angle = 0;     // 0-180 degrees (0 = closed)
            static int last_encoder_pos_servo = 0;
            static bool first_entry_servo = true;
            static bool was_pressed_servo = false;
            static bool adjusting_angle = false;

            if (first_entry_servo) {
                last_encoder_pos_servo = enc.getPosition();
                first_entry_servo = false;
                was_pressed_servo = enc.isPressed();  // Capture current state to ignore carry-over press
                test_servo_selected = 0;
                test_servo_angle = 0;  // Start at closed position
                adjusting_angle = false;
                // Close all servos
                for (int i = 0; i < 3; i++) {
                    servos[i]->writeDegrees(0.0f);
                }
            }

            int pos = enc.getPosition();
            int delta = pos - last_encoder_pos_servo;
            bool pressed = enc.isPressed();

            if (adjusting_angle) {
                // Encoder changes angle
                if (delta != 0) {
                    test_servo_angle += delta * 5;  // 5 degree steps
                    if (test_servo_angle < 0) test_servo_angle = 0;
                    if (test_servo_angle > 180) test_servo_angle = 180;
                    last_encoder_pos_servo = pos;
                    // Apply angle to selected servo
                    servos[test_servo_selected]->writeDegrees((float)test_servo_angle);
                }

                // Button exits angle mode
                if (pressed && !was_pressed_servo) {
                    adjusting_angle = false;
                    // Close servo when exiting
                    servos[test_servo_selected]->writeDegrees(0.0f);
                    test_servo_angle = 0;
                }
            } else {
                // Encoder selects servo (0, 1, 2) or Back (3)
                if (delta != 0) {
                    test_servo_selected += delta;
                    if (test_servo_selected < 0) test_servo_selected = 0;
                    if (test_servo_selected > 3) test_servo_selected = 3;
                    last_encoder_pos_servo = pos;
                }

                // Button enters angle mode or goes back
                if (pressed && !was_pressed_servo) {
                    if (test_servo_selected < 3) {
                        adjusting_angle = true;
                        test_servo_angle = 0;  // Start at closed
                        servos[test_servo_selected]->writeDegrees(0.0f);
                    } else {
                        // Back to test menu
                        first_entry_servo = true;
                        current = ScreenId::TestMenu;
                    }
                }
            }
            was_pressed_servo = pressed;

            // Update display
            char line[21];
            if (adjusting_angle) {
                std::snprintf(line, sizeof(line), "Servo %d: %3d deg   ", test_servo_selected + 1, test_servo_angle);
                lcd.setCursor(2, 0);
                lcd.print(line);
                lcd.setCursor(3, 0);
                lcd.print("  Turn to adjust    ");
            } else {
                // Show selection
                std::snprintf(line, sizeof(line), "%s1%s %s2%s %s3%s %sBack%s",
                    test_servo_selected == 0 ? "[" : " ", test_servo_selected == 0 ? "]" : " ",
                    test_servo_selected == 1 ? "[" : " ", test_servo_selected == 1 ? "]" : " ",
                    test_servo_selected == 2 ? "[" : " ", test_servo_selected == 2 ? "]" : " ",
                    test_servo_selected == 3 ? "[" : " ", test_servo_selected == 3 ? "]" : " ");
                lcd.setCursor(2, 0);
                lcd.print(line);
                lcd.setCursor(3, 0);
                lcd.print("Press to test       ");
            }

            // Show angle on 7-segment
            sevenSeg->clear();
            if (adjusting_angle) {
                // Color based on position: red at 0, green at 90, blue at 180
                uint8_t r = test_servo_angle < 90 ? (90 - test_servo_angle) * 2 : 0;
                uint8_t g = 255 - abs(test_servo_angle - 90) * 2;
                uint8_t b = test_servo_angle > 90 ? (test_servo_angle - 90) * 2 : 0;
                sevenSeg->printNumber(test_servo_angle, r, g, b);
            } else {
                sevenSeg->printNumber(test_servo_selected + 1, 0, 255, 0);
            }
            sevenSeg->show();

            sleep_ms(50);
            break;
        }
        }
    }
}
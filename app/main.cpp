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
#include "screens.hpp"

#include "wifi_config.h"
#include "dispenser_state.h"
#include "web_server.h"
#include "dhserver.h"
#include "lwip/apps/mdns.h"

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

// Scale content names ("Wheat", "Spelt", ...) - mirrored in g_state.names for
// the web UI and persisted to flash. Deferred like the PID save: flash writes
// stall IRQs ~100 ms and must never land mid-dispense.
static bool name_save_pending = false;

static void save_scale_names() {
    NameConfig nc;
    for (int i = 0; i < 3; i++) {
        std::snprintf(nc.names[i], SCALE_NAME_LEN, "%s", g_state.names[i]);
    }
    save_name_config(nc);
}

// Per-servo zero (flow-start) angles - mirrored in g_state.servo_zero and
// persisted to flash. Same deferred-save rule as the PID gains and names.
static bool servo_save_pending = false;

static void save_servo_zeros() {
    ServoConfig svc;
    for (int i = 0; i < 3; i++) {
        svc.open_deg[i] = g_state.servo_zero[i];
    }
    save_servo_config(svc);
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

// Announce this device as http://korn.local via mDNS/Bonjour so a printed QR
// code works regardless of the DHCP-assigned IP. netif_default is the active
// interface in both STA and AP mode. Call after the IP is up.
static void start_mdns() {
    cyw43_arch_lwip_begin();
    mdns_resp_init();
    mdns_resp_add_netif(netif_default, "korn");
    mdns_resp_add_service(netif_default, "korn", "_http", DNSSD_PROTO_TCP, 80, NULL, NULL);
    mdns_resp_announce(netif_default);
    cyw43_arch_lwip_end();
    printf("[mdns] korn.local up\n");
}

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

    // Load persisted scale content names
    {
        NameConfig nc;
        if (load_name_config(nc)) {
            for (int i = 0; i < 3; i++) {
                std::snprintf(g_state.names[i], sizeof(g_state.names[i]), "%s", nc.names[i]);
            }
        }
    }

    // Load persisted servo zero angles (before the startup close below, which
    // parks each servo relative to its zero)
    {
        ServoConfig svc;
        if (load_servo_config(svc)) {
            for (int i = 0; i < 3; i++) {
                g_state.servo_zero[i] = svc.open_deg[i];
            }
        }
    }

    // Link servo1 + vib3 on shared PWM slice 3 before any servo/vibrator output, so
    // the slice frequency is arbitrated from the very first startup close below.
    servo1.attachShared(&slice3_pwm);
    vib3.attachShared(&slice3_pwm);

    // Close all servos at startup, then release (no holding torque)
    for (int i = 0; i < 3; i++) {
        servos[i]->writeDegrees(servo_close(g_state, i));
    }
    sleep_ms(500);  // Give servos time to reach position
    for (int i = 0; i < 3; i++) {
        servos[i]->off();  // Release - no holding torque
    }

    // Give USB serial a moment to connect (non-blocking)
    sleep_ms(1000);
    printf("Ready.\r\r\n");

    lcd.init(PICO_DEFAULT_I2C_SDA_PIN, PICO_DEFAULT_I2C_SCL_PIN, 100000);

    // --- Network mode chooser --------------------------------------------
    // Pick Home WiFi (STA, still falls back to the hotspot if it fails) or
    // Hotspot (AP) with the encoder. Auto-continues with the remembered
    // choice after 5 s, so unattended power cycles never wait on a person.
    uint8_t net_mode = 0;  // 0 = Home WiFi, 1 = Hotspot
    {
        NetConfig ncfg;
        if (load_net_config(ncfg)) net_mode = ncfg.mode;

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Network?");

        int  sel = net_mode;
        int  last_sel = -1, last_secs = -1;
        int  last_pos = enc.getPosition();
        absolute_time_t deadline = make_timeout_time_ms(5000);

        // Glitch protection: right after boot the button line can show a
        // spurious edge and the encoder's stable-position filter settles with
        // one jump - either instantly "confirmed" the menu. So: ignore
        // position changes during a warm-up, arm the button only after it has
        // read released continuously for 150 ms, and require two consecutive
        // pressed reads (~60 ms) to confirm.
        absolute_time_t warmup = make_timeout_time_ms(300);
        absolute_time_t released_since = get_absolute_time();
        bool armed = false;
        int  press_reads = 0;

        while (!time_reached(deadline)) {
            int pos = enc.getPosition();
            if (pos != last_pos) {
                if (time_reached(warmup)) {
                    sel = (pos > last_pos) ? 1 : 0;  // two entries: turn down/up
                    deadline = make_timeout_time_ms(5000);  // interaction resets timer
                }
                last_pos = pos;
            }

            if (enc.isPressed()) {
                if (armed && ++press_reads >= 2) {
                    printf("[net] chooser: confirmed by press\n");
                    break;
                }
                released_since = get_absolute_time();  // not armed: keep waiting
            } else {
                press_reads = 0;
                if (!armed &&
                    absolute_time_diff_us(released_since, get_absolute_time()) >= 150000) {
                    armed = true;
                }
            }

            if (sel != last_sel) {
                lcd.setCursor(1, 0);
                lcd.print(sel == 0 ? "> Home WiFi    " : "  Home WiFi    ");
                lcd.setCursor(2, 0);
                lcd.print(sel == 1 ? "> Hotspot      " : "  Hotspot      ");
                last_sel = sel;
            }
            int secs = (int)((absolute_time_diff_us(get_absolute_time(), deadline)
                              + 999999) / 1000000);
            if (secs != last_secs) {
                char cdl[21];
                std::snprintf(cdl, sizeof(cdl), "Auto in %ds  Press=OK", secs);
                lcd.setCursor(3, 0);
                lcd.print(cdl);
                last_secs = secs;
            }
            sleep_ms(30);
        }

        printf("[net] chooser result: %s\n", sel == 1 ? "Hotspot" : "Home WiFi");
        if ((uint8_t)sel != net_mode) {
            net_mode = (uint8_t)sel;
            ncfg.mode = net_mode;
            save_net_config(ncfg);  // remembered as next boot's default
        }
    }

    // --- WiFi initialization ---
    if (cyw43_arch_init()) {
        printf("[wifi] init failed\n");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi init failed!");
        sleep_ms(2000);
    } else {
        net_stack_up = true;

        // Hotspot chosen at the boot menu skips the STA attempts entirely
        bool force_ap = (net_mode == 1);

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
            char ip_line[21];
            std::snprintf(ip_line, sizeof(ip_line), "IP: %s", ip_str);
            lcd.print(ip_line);
            lcd.setCursor(3, 0);
            lcd.print("http://korn.local");

            // Start web server + mDNS (korn.local)
            web_server_init(&g_state, WEB_SERVER_PORT);
            start_mdns();
            sleep_ms(2000);
        } else {
            // STA failed (or AP forced) - broadcast our own network instead
            if (!force_ap) printf("[wifi] all attempts failed: %d\n", wifi_err);
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(force_ap ? "Hotspot mode" : "WiFi failed - AP up");

            if (start_ap_mode()) {
                g_state.ap_mode = true;
                lcd.setCursor(1, 0);
                lcd.print("AP: " WIFI_AP_SSID);
                lcd.setCursor(2, 0);
                lcd.print("Pass: " WIFI_AP_PASSWORD);
                lcd.setCursor(3, 0);
                lcd.print("http://192.168.4.1");

                web_server_init(&g_state, WEB_SERVER_PORT);
                start_mdns();   // korn.local works in AP mode too
                sleep_ms(4000);
            } else {
                lcd.setCursor(3, 0);
                lcd.print("Continuing offline..");
                sleep_ms(3000);
            }
        }
    }

    bz.playMacStartup(); // Mac-like startup chime

    // Initialize 7-segment display after encoder to avoid PIO conflict.
    // Full brightness for the boot animation; dimmed to ~1/3 after it.
    sevenSegStrip = new Ws2812(SEVENSEG_PIN, SEVENSEG_LEDS);
    sevenSeg = new SevenSeg(*sevenSegStrip, LAYOUT_6DIGITS);

    // --- 7-segment startup animation: random segments sweep left to right ---
    {
        const uint8_t ORANGE_R = 255, ORANGE_G = 80, ORANGE_B = 0;
        const int SWEEP_DELAY_MS = 40;  // Speed of sweep
        const int HOLD_MS = 3300;       // Hold at end before clearing

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

    // Normal operation runs dimmed (~1/3) - full white was blinding
    sevenSegStrip->setBrightness(85);

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

    // UI context shared by the screen classes (app/screens.cpp) and the web
    // command dispatcher below
    UiContext ctx{
        lcd, enc, bz, scales, servos, vibrators, sevenSeg, sc, g_state,
        Kp, Ki, Kd, dispense_pid, &net_lock, &net_unlock
    };
    ctx.names = g_state.names;

    ScreenManager mgr;
    mgr.init(ctx, isConfigured ? ScreenId::Menu : ScreenId::SelectScale);

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
        float gr_sel = 0.0f, gr_other = 0.0f;
        int other = (ctx.selected_scale + 1 + bg_weight_cycle) % 3;
        if (absolute_time_diff_us(last_bg_weight_time, get_absolute_time()) > 250000) {
            w_sel = scales[ctx.selected_scale]->read_weight();
            gr_sel = scales[ctx.selected_scale]->last_gross();   // same raw sample
            if (other != ctx.selected_scale) {
                w_other = scales[other]->read_weight();
                gr_other = scales[other]->last_gross();
            }
            have_weights = true;
            bg_weight_cycle = (bg_weight_cycle + 1) % 2;
            last_bg_weight_time = get_absolute_time();
        }

        net_lock();
        g_state.selected_scale = ctx.selected_scale;
        g_state.target_grams = ctx.target_grams;
        if (have_weights) {
            g_state.weights[ctx.selected_scale] = w_sel;
            g_state.gross[ctx.selected_scale] = gr_sel;
            if (other != ctx.selected_scale) {
                g_state.weights[other] = w_other;
                g_state.gross[other] = gr_other;
            }
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
        // Drain ALL queued commands in order (the queue means nothing gets lost
        // while a blocking tare/calibrate stalls the loop). Pop under the lwIP
        // lock, dispatch from the copy.
        while (true) {
            WebCmd c;
            net_lock();
            bool have = (g_state.cmd_tail != g_state.cmd_head);
            if (have) {
                c = g_state.cmd_queue[g_state.cmd_tail];
                g_state.cmd_tail = (uint8_t)((g_state.cmd_tail + 1) % WEBCMD_QUEUE_LEN);
            }
            net_unlock();
            if (!have) break;

            ctx.web_active = true;  // Web is in control, disable hardware input

            switch (c.cmd) {
            case WebCommand::Tare:
                // tare blocks ~1.6 s - never stall the PID with the gate open
                if (g_state.dispensing) break;
                scales[ctx.selected_scale]->tare();
                bz.playMarioCoin();
                // Publish the tared reading immediately so the next status poll
                // shows ~0 instead of the stale pre-tare weight
                {
                    float wnew = scales[ctx.selected_scale]->read_weight();
                    net_lock();
                    g_state.weights[ctx.selected_scale] = wnew;
                    g_state.gross[ctx.selected_scale] = scales[ctx.selected_scale]->last_gross();
                    net_unlock();
                }
                break;

            case WebCommand::SetTarget:
                ctx.target_grams = c.i0;
                if (ctx.target_grams < 1) ctx.target_grams = 1;
                if (ctx.target_grams > 9999) ctx.target_grams = 9999;
                break;

            case WebCommand::SelectScale:
                if (c.i0 >= 0 && c.i0 <= 2) {
                    ctx.selected_scale = c.i0;
                }
                break;

            case WebCommand::StartDispense:
                // Jump to the dispense screen and flag it to auto-start
                if (!g_state.dispensing) {
                    ctx.web_start_dispense = true;
                    g_state.dispense_done = false;
                    mgr.goTo(ctx, ScreenId::Dispense);
                }
                break;

            case WebCommand::StopDispense:
                // Route through the Dispense screen's Running state so the state
                // machine, telemetry and web state all stop consistently. (Closing
                // the servo here alone is not enough - Running would re-open it.)
                if (g_state.dispensing) {
                    ctx.web_stop_dispense = true;
                }
                break;

            case WebCommand::TestServo: {
                // i0 = explicit servo index (calibration UI); -1 = legacy test
                // slider, which follows the selected scale
                int idx = (c.i0 >= 0 && c.i0 <= 2) ? c.i0 : ctx.selected_scale;
                if (g_state.dispensing) break;  // never fight the PID loop
                if (c.f0 < 0.0f) {
                    // Close-and-release sentinel
                    servos[idx]->writeDegrees(servo_close(g_state, idx));
                    sleep_ms(300);
                    servos[idx]->off();
                } else {
                    servos[idx]->writeDegrees(c.f0);  // driver clamps 0-180
                }
                break;
            }

            case WebCommand::TestVibrator:
                vibrators[ctx.selected_scale]->setIntensity(c.f0);
                break;

            case WebCommand::TestStop:
                for (int i = 0; i < 3; i++) {
                    servos[i]->writeDegrees(servo_close(g_state, i));
                    vibrators[i]->off();
                }
                sleep_ms(300);
                for (int i = 0; i < 3; i++) {
                    servos[i]->off();
                }
                break;

            case WebCommand::SetPID:
                Kp = (double)c.f0;
                Ki = (double)c.f1;
                Kd = (double)c.f2;
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

            case WebCommand::SetName:
                if (c.i0 >= 0 && c.i0 <= 2) {
                    net_lock();
                    std::snprintf(g_state.names[c.i0], sizeof(g_state.names[c.i0]),
                                  "%s", c.s0);
                    net_unlock();
                    if (!g_state.dispensing) {
                        save_scale_names();
                    } else {
                        name_save_pending = true;
                    }
                }
                break;

            case WebCommand::SetServoZero:
                if (c.i0 >= 0 && c.i0 <= 2 && c.f0 >= 0.0f && c.f0 <= 180.0f) {
                    net_lock();
                    g_state.servo_zero[c.i0] = c.f0;
                    net_unlock();
                    bz.playMarioCoin();
                    if (!g_state.dispensing) {
                        save_servo_zeros();
                        // Park at the new closed position (zero - backoff)
                        servos[c.i0]->writeDegrees(servo_close(g_state, c.i0));
                        sleep_ms(300);
                        servos[c.i0]->off();
                    } else {
                        servo_save_pending = true;
                    }
                }
                break;

            case WebCommand::EStop:
                // Emergency stop: works from ANY state, never asks questions.
                printf("[estop] web emergency stop\n");
                if (g_state.dispensing) {
                    // Route through the Dispense screen's Running state (same
                    // tick, drain runs before mgr.tick) - servo closes, PID
                    // goes MANUAL, telemetry run ends cleanly
                    ctx.web_stop_dispense = true;
                } else {
                    // Idle/test/jog: close and release everything
                    for (int i = 0; i < 3; i++) {
                        servos[i]->writeDegrees(servo_close(g_state, i));
                    }
                    sleep_ms(300);
                    for (int i = 0; i < 3; i++) {
                        servos[i]->off();
                    }
                }
                for (int i = 0; i < 3; i++) {
                    vibrators[i]->off();
                }
                break;

            case WebCommand::Calibrate:
                // Blocks even longer than tare, and writes flash - not while
                // a dispense is running
                if (g_state.dispensing) break;
                if (c.i0 > 0) {
                    scales[ctx.selected_scale]->calibrate_scale(
                        (float)c.i0, 10);
                    // The tare done before calibration IS the calibrated zero
                    scales[ctx.selected_scale]->set_cal_offset(
                        scales[ctx.selected_scale]->get_offset());
                    sc.entries[ctx.selected_scale].offset_counts =
                        scales[ctx.selected_scale]->get_offset();
                    sc.entries[ctx.selected_scale].count_per_g =
                        scales[ctx.selected_scale]->get_scale();
                    save_scale_config(sc);
                    bz.playMarioCoin();
                }
                break;

            default:
                break;
            }
        }

        // Flush saves that were deferred because a dispense was running
        if (pid_save_pending && !g_state.dispensing) {
            pid_save_pending = false;
            save_pid_gains();
        }
        if (name_save_pending && !g_state.dispensing) {
            name_save_pending = false;
            save_scale_names();
        }
        if (servo_save_pending && !g_state.dispensing) {
            servo_save_pending = false;
            save_servo_zeros();
        }
        // The LCD Servo Zero screen requests persistence through this flag
        if (ctx.servo_zero_save_request && !g_state.dispensing) {
            ctx.servo_zero_save_request = false;
            save_servo_zeros();
        }

        // When web is active, show status on LCD but skip all hardware input.
        // EXCEPTION: the Dispense screen must keep running - it owns the PID
        // loop, so a web-started dispense would otherwise never actually run.
        if (ctx.web_active && mgr.currentId() != ScreenId::Dispense) {
            static bool web_lcd_drawn = false;

            // Encoder press (while idle) hands control back to the local UI.
            // Close + release all servos first: one may have been left jogged
            // open from the phone (calibration/test) and must not stay open.
            if (enc.isPressed() && !g_state.dispensing) {
                for (int i = 0; i < 3; i++) {
                    servos[i]->writeDegrees(servo_close(g_state, i));
                }
                sleep_ms(300);
                for (int i = 0; i < 3; i++) {
                    servos[i]->off();
                }
                ctx.web_active = false;
                web_lcd_drawn = false;
                mgr.goTo(ctx, ScreenId::Menu);
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
            float wg = scales[ctx.selected_scale]->read_weight();
            std::snprintf(wline, sizeof(wline), "Scale %d: %d g      ", ctx.selected_scale + 1, (int)(wg + 0.5f));
            lcd.setCursor(1, 0);
            lcd.print(wline);
            std::snprintf(wline, sizeof(wline), "Target: %d g        ", ctx.target_grams);
            lcd.setCursor(2, 0);
            lcd.print(wline);
            if (g_state.dispensing) {
                std::snprintf(wline, sizeof(wline), "Dispensing: %d g    ", (int)(g_state.dispensed_grams + 0.5f));
            } else if (g_state.ap_mode) {
                // RSSI is meaningless as an access point - show where the
                // app lives instead (\xA5 = centered dot in the HD44780 ROM)
                std::snprintf(wline, sizeof(wline), "Hotspot\xA5 192.168.4.1");
            } else {
                int32_t rssi = -100;
                cyw43_wifi_get_rssi(&cyw43_state, &rssi);
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
            continue;  // Web owns the UI; skip the screen tick
        }

        // Run the current screen (see app/screens.cpp)
        mgr.tick(ctx);
    }
}

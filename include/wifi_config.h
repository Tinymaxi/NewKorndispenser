#ifndef _WIFI_CONFIG_H
#define _WIFI_CONFIG_H

// Credentials (WIFI_SSID, WIFI_PASSWORD, WIFI_AP_PASSWORD) live in
// wifi_secrets.h, which is gitignored. First-time setup: copy
// include/wifi_secrets.h.example to include/wifi_secrets.h and fill it in.
#if !__has_include("wifi_secrets.h")
#error "include/wifi_secrets.h missing - copy wifi_secrets.h.example to wifi_secrets.h and fill in your credentials"
#endif
#include "wifi_secrets.h"

// Access-point fallback (when the router is unreachable, the Pico broadcasts
// its own network; browse to http://192.168.4.1)
#define WIFI_AP_SSID "KornDispenser"

// Web server port
#define WEB_SERVER_PORT 80

#endif // _WIFI_CONFIG_H

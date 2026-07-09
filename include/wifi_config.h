#ifndef _WIFI_CONFIG_H
#define _WIFI_CONFIG_H

// WiFi credentials - change these to match your network
#define WIFI_SSID     "FRITZ!Box 5530 AO"
#define WIFI_PASSWORD "39124527370000908109"

// Access-point fallback (when the router is unreachable, the Pico broadcasts
// its own network; browse to http://192.168.4.1)
#define WIFI_AP_SSID     "KornDispenser"
#define WIFI_AP_PASSWORD "dispense123"   // WPA2 - must be >= 8 chars

// Web server port
#define WEB_SERVER_PORT 80

#endif // _WIFI_CONFIG_H

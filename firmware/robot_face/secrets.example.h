#ifndef _ROBOT_SECRETS_H
#define _ROBOT_SECRETS_H

// Copy this file to secrets.h (same folder) and fill in your networks.
// secrets.h is listed in .gitignore, so your passwords stay on your computer.

// Home network: 2.4 GHz only (the ESP32-C3 has no 5 GHz).
// Networks that need a username login (like eduroam) won't work, use home WiFi or a phone hotspot.
// Leave empty ("") to run without internet.
#define HOME_WIFI_SSID ""
#define HOME_WIFI_PASS ""

// Second network, tried when the home one isn't found (for example a phone hotspot set to
// 2.4 GHz). Leave empty if not used. Type "wifi" in the Serial Monitor to see which 2.4 GHz
// networks the robot can find.
#define BACKUP_WIFI_SSID ""
#define BACKUP_WIFI_PASS ""

// Password Arduino IDE asks for when uploading over WiFi. Without this line the robot WiFi
// password (AP_PASS in config.h) is used.
// #define OTA_PASS "pick-a-password"

#endif

#ifndef _ROBOT_CONFIG_H
#define _ROBOT_CONFIG_H

// All settings you are likely to change are in this file.

//*********************************************************************************************
//  DISPLAY - ELEGOO 0.96" OLED (SSD1306, 128x64, I2C)
//*********************************************************************************************

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1      // no reset pin
#define OLED_ADDRESS 0x3C  // ELEGOO modules use 0x3C
#define OLED_SDA 6         // D4
#define OLED_SCL 7         // D5


//*********************************************************************************************
//  MOTORS
//*********************************************************************************************

// Pins labeled A0-A3 (= D0-D3) on the board, given as GPIO numbers
#define MOTOR_IN1 2        // A0 / D0, left motor
#define MOTOR_IN2 3        // A1 / D1, left motor
#define MOTOR_IN3 4        // A2 / D2, right motor
#define MOTOR_IN4 5        // A3 / D3, right motor
#define INVERT_LEFT false  // set true if the left wheel spins backwards on "forward"
#define INVERT_RIGHT false // set true if the right wheel spins backwards on "forward"

// Motor power tuning, all PWM values are 0-255. Try changes live with the commands
// "minpwm 160", "maxpwm 200", "kick 80", then put the values that work here.
// The L298N loses about 2-3 V inside, so small motors need a high minimum.
#define MOTOR_MIN_PWM 80   // lowest power that still turns the wheels (about 80 for DRV8833, 150 for L298N)
#define MOTOR_MAX_PWM 255  // lower this if the motors get hot or run too fast at full speed
#define MOTOR_KICK_MS 80   // full-power pulse that breaks a wheel loose when it starts, 0 = off
#define MOTOR_PWM_HZ 500   // PWM frequency, low values give small motors more push at low speed


//*********************************************************************************************
//  WIFI
//*********************************************************************************************

// Your home network: control the robot from any phone/laptop on the same WiFi at
// http://roboface.local (or the IP address shown on the OLED), and get clock + weather.
// 2.4 GHz only (the ESP32-C3 has no 5 GHz).
// Networks that need a username login (like eduroam) won't work, use home WiFi or a phone hotspot.
// WiFi names and passwords are in secrets.h, which git ignores so they never reach GitHub.
// First time: copy secrets.example.h to secrets.h and fill it in.
#include "secrets.h"
#define HOSTNAME "roboface" // -> http://roboface.local

// The robot's own network, for control without a router at http://192.168.4.1
// (always on when no home WiFi is set)
#define ROBOT_AP_ENABLED true
#define AP_SSID "RoboFace"
#define AP_PASS "robot1234" // at least 8 characters


//*********************************************************************************************
//  CLOCK AND WEATHER
//*********************************************************************************************

// true = find the location from the internet connection (IP address, city level accurate) and
// take the weather and time zone from there, so it still works when the robot moves to another
// WiFi. The values below are then only used until the lookup succeeds or if it fails.
// false = always use the values below.
#define AUTO_LOCATION true

// Time zone as a POSIX TZ string, examples:
//   US Eastern  "EST5EDT,M3.2.0,M11.1.0"     US Central "CST6CDT,M3.2.0,M11.1.0"
//   US Mountain "MST7MDT,M3.2.0,M11.1.0"     US Pacific "PST8PDT,M3.2.0,M11.1.0"
//   UK "GMT0BST,M3.5.0/1,M10.5.0"   Central Europe "CET-1CEST,M3.5.0,M10.5.0/3"   China "CST-8"
#define TIME_ZONE "EST5EDT,M3.2.0,M11.1.0"
#define CLOCK_24H false

// Location for the weather (decimal degrees, find them on Google Maps by right-clicking a spot)
#define WEATHER_LAT "42.44"   // Ithaca, NY
#define WEATHER_LON "-76.50"
#define WEATHER_FAHRENHEIT true  // false = Celsius
#define WEATHER_REFRESH_MIN 15


//*********************************************************************************************
//  SLEEP MODE AND SCREEN PROTECTION
//*********************************************************************************************

#define SLEEP_AFTER_MIN 5   // fall asleep (clock screen) after this many minutes without commands
#define PIXEL_SHIFT_SEC 60  // move the clock screen a few pixels this often, so no pixel stays lit
#define NIGHT_OFF_START 23  // while asleep, turn the screen fully off from this hour...
#define NIGHT_OFF_END 7     // ...until this hour (set both the same to never turn it off)


//*********************************************************************************************
//  BEHAVIOR
//*********************************************************************************************

#define AUTO_MODE_AT_START false  // true = wander around on its own from power-on (careful on tables!)
#define MANUAL_SPEED 70           // default manual driving speed in percent, change live with "speed"
#define MANUAL_TIMEOUT_MS 700     // one drive command drives this long, controllers repeat it while a
                                  // key is held, so the robot stops by itself if the connection drops

#endif

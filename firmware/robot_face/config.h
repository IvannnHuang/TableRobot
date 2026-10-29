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
// "minpwm 100", "maxpwm 200", "kick 80", then put the values that work here.
// The DRV8833 loses very little voltage, so a low minimum works (an L298N needs about 150).
#define MOTOR_MIN_PWM 80   // lowest power that still turns the wheels (about 80 for DRV8833, 150 for L298N)
#define MOTOR_MAX_PWM 255  // lower this if the motors get hot or run too fast at full speed
#define MOTOR_KICK_MS 80   // full-power pulse that breaks a wheel loose when it starts, 0 = off
#define MOTOR_PWM_HZ 500   // PWM frequency, low values give small motors more push at low speed
#define MOTOR_SLOW_DECAY true // true = motor brakes between PWM pulses, so the speed slider really changes
                              // the speed (DRV8833). false = coasts between pulses (old behavior)


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

// After start-up the WiFi address stays on the OLED until a phone or laptop opens the web page,
// but at most this long (a still picture for too long can burn into the OLED)
#define WIFI_INFO_MAX_MIN 10
// The phone counts as gone (and the WiFi address comes back) when it leaves the robot's WiFi,
// or when the open page sends nothing for this long (it asks for the status every 2 s)
#define WEB_LOST_SEC 10

// Updates over WiFi: Arduino IDE shows the robot under Tools > Port as "roboface at 192.168...".
// Needs Partition Scheme "Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)", flashed once over USB.
// Password: OTA_PASS in secrets.h, or AP_PASS if that isn't set.
#define OTA_ENABLED true
#ifdef OTA_PASS
#define OTA_PASSWORD OTA_PASS
#else
#define OTA_PASSWORD AP_PASS
#endif


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
#define NIGHT_OFF_START 23  // while asleep, show only a small moving clock from this hour...
#define NIGHT_OFF_END 7     // ...until this hour (set both the same for the full clock all night)
#define NIGHT_SMALL_CLOCK true // false = screen fully off at night instead of the small clock
#define SLEEP_CONTRAST 24   // brightness of the sleep screen, 0-255 (0 is too dark on some panels)


//*********************************************************************************************
//  BEHAVIOR
//*********************************************************************************************

#define AUTO_MODE_AT_START false  // true = wander around on its own from power-on (careful on tables!)
#define MANUAL_SPEED 70           // default manual driving speed in percent, change live with "speed"
#define MANUAL_TIMEOUT_MS 700     // one drive command drives this long, controllers repeat it while a
                                  // key is held, so the robot stops by itself if the connection drops


//*********************************************************************************************
//  PERSONALITY (used while the mood is "auto", the default; "mood happy" etc. turn it off)
//*********************************************************************************************

#define MORNING_START 7      // happy eyes from this hour...
#define MORNING_END 10       // ...until this hour
#define NIGHT_TIRED_START 22 // tired eyes from this hour until MORNING_START
#define HOT_TEMP (WEATHER_FAHRENHEIT ? 86 : 30)  // sweats when it's this hot outside or hotter
#define COLD_TEMP (WEATHER_FAHRENHEIT ? 32 : 0)  // shivers now and then when it's this cold or colder
#define BORED_AFTER_MIN 2    // asks for attention after this many minutes alone (0 = never)
#define BORED_WIGGLE true    // false = only the eyes ask for attention, the wheels stay still


//*********************************************************************************************
//  NOTES, TIMERS AND RECORDING
//*********************************************************************************************

#define NOTE_MAX_LEN 80          // longest message, the OLED shows about 60 characters at once
#define REMINDER_SHOW_MIN 5      // a reminder disappears by itself after this long
#define ALARM_RING_MIN 3         // the alarm dances on and off this long unless it's stopped
#define FOCUS_BREAK_MIN 5        // break after a focus timer
#define RECORD_MAX_SEC 60        // longest recorded dance


//*********************************************************************************************
//  POWER SAVING
//*********************************************************************************************

#define CPU_MHZ 160              // 160 = full speed. 80 saves a few mA, but drawing and web replies get slower
// Turn the robot's own WiFi off while the home WiFi is connected and no phone is on it.
// It comes back by itself when the home WiFi is lost, or with the "ap on" command.
#define AP_OFF_WHEN_HOME true
#define AP_OFF_AFTER_MIN 3       // ...after this many minutes without a phone on it

#endif

  // Robot face + wheels for Seeed XIAO ESP32-C3
//
// Hardware
//   OLED ELEGOO 0.96" SSD1306 128x64 (I2C): SDA -> D4 (GPIO6), SCL -> D5 (GPIO7)
//   DRV8833:     IN1 -> A0 (GPIO2), IN2 -> A1 (GPIO3)   left motor  (OUT1/OUT2)
//                IN3 -> A2 (GPIO4), IN4 -> A3 (GPIO5)   right motor (OUT3/OUT4)
//                1 kOhm on each IN wire, EEP -> 3V3, GND shared with the XIAO GND
//
// Arduino IDE: Board "XIAO_ESP32C3", USB CDC On Boot "Enabled",
//              Partition Scheme "Huge APP (3MB No OTA/1MB SPIFFS)" (the default only just fits)
// Libraries (Arduino Library Manager): Adafruit SSD1306 (also installs Adafruit GFX)
// Files in this folder: config.h (your settings), motors.h, weather.h, clock_screen.h,
//                       FluxGarage_RoboEyes.h
//
// Control (all use the same text commands, type "help" for the list)
//   Same WiFi:  http://roboface.local or the IP shown on the OLED, from a phone or laptop browser
//   Robot WiFi: join "RoboFace" (password "robot1234"), open http://192.168.4.1
//   USB cable:  laptop_control/serial_control.html in Chrome/Edge, or the Arduino Serial Monitor
//
// Modes
//   REST     - stands still, eyes look around, blinks
//   BEHAVIOR - plays a choreographed sequence of wheel moves and expressions
//   MANUAL   - driven by a controller, face follows the direction of travel
//   SLEEP    - eyes close, then a dimmed clock with date and weather is shown
//   Auto mode (off by default) picks a random behavior every few seconds while resting.
//   With no commands for SLEEP_AFTER_MIN minutes the robot falls asleep; any command wakes it.

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "motors.h"
#include "weather.h"
#include "clock_screen.h"
// Include RoboEyes last: it defines short macros (N, E, S, W, ON, OFF, DEFAULT...)
// that could clash with names inside other libraries' headers.
#undef DEFAULT // the ESP32 core also defines DEFAULT (unused here), RoboEyes needs its own
#include "FluxGarage_RoboEyes.h"


//*********************************************************************************************
//  OBJECTS
//*********************************************************************************************

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RoboEyes<Adafruit_SSD1306> roboEyes(display);
ClockScreen clockScreen(display);
Drive drive(MOTOR_IN1, MOTOR_IN2, MOTOR_IN3, MOTOR_IN4, INVERT_LEFT, INVERT_RIGHT,
            MOTOR_MIN_PWM, MOTOR_MAX_PWM, MOTOR_KICK_MS, MOTOR_PWM_HZ);
WebServer server(80);


//*********************************************************************************************
//  BEHAVIORS
//*********************************************************************************************

// Face effects for a step, combine with |
#define FX_BLINK    0x01 // blink once
#define FX_LAUGH    0x02 // eyes bounce up and down (one shot)
#define FX_CONFUSED 0x04 // eyes shake left and right (one shot)
#define FX_SWEAT    0x08 // sweat drops for the whole step
#define FX_CURIOUS  0x10 // outer eye grows when looking sideways
#define FX_SHIVER   0x20 // small nervous horizontal shiver for the whole step
#define FX_CLOSE    0x40 // keep eyes closed for the whole step

#define KEEP 255 // for Step.look: don't change where the eyes look

// One step of a behavior: wheel speeds (percent) and the face to show for a duration
struct Step {
  uint16_t ms;
  int8_t left, right; // -100..100
  uint8_t mood;       // DEFAULT, TIRED, ANGRY, HAPPY
  uint8_t look;       // DEFAULT (center), N, NE, E, SE, S, SW, W, NW or KEEP
  uint8_t fx;         // FX_* flags
};

struct Behavior {
  const char *name;
  const Step *steps;
  uint8_t count;
};

#define BEHAVIOR(name, steps) { name, steps, sizeof(steps) / sizeof(steps[0]) }

// Wake up slowly, then smile (no driving)
const Step wakeUp[] = {
  { 1200,   0,   0, TIRED,   DEFAULT, FX_CLOSE },
  {  800,   0,   0, TIRED,   DEFAULT, 0 },
  {  600,   0,   0, DEFAULT, DEFAULT, FX_BLINK },
  {  400,   0,   0, DEFAULT, W,       0 },
  {  400,   0,   0, DEFAULT, E,       0 },
  { 1200,   0,   0, HAPPY,   DEFAULT, FX_LAUGH },
};

// Get drowsy and close the eyes, then the clock screen takes over
const Step fallAsleep[] = {
  { 1200,   0,   0, TIRED,   S,       0 },
  {  800,   0,   0, TIRED,   S,       FX_BLINK },
  {  600,   0,   0, TIRED,   S,       0 },
  { 1500,   0,   0, TIRED,   S,       FX_CLOSE },
};

// Wiggle left and right with a big smile
const Step happyDance[] = {
  {  300, -60,  60, HAPPY, W,       FX_LAUGH },
  {  300,  60, -60, HAPPY, E,       0 },
  {  300, -60,  60, HAPPY, W,       0 },
  {  300,  60, -60, HAPPY, E,       FX_LAUGH },
  {  300, -60,  60, HAPPY, W,       0 },
  {  300,  60, -60, HAPPY, E,       0 },
  { 1000,   0,   0, HAPPY, DEFAULT, FX_LAUGH },
};

// Look first, then turn and go: eyes always lead the wheels
const Step explore[] = {
  {  800,   0,   0, DEFAULT, E,       FX_CURIOUS },
  {  450,  50, -50, DEFAULT, E,       FX_CURIOUS },
  { 1200,  55,  55, DEFAULT, N,       0 },
  {  500,   0,   0, DEFAULT, DEFAULT, FX_BLINK },
  {  800,   0,   0, DEFAULT, W,       FX_CURIOUS },
  {  450, -50,  50, DEFAULT, W,       FX_CURIOUS },
  { 1000,  55,  55, DEFAULT, N,       0 },
  {  800,   0,   0, HAPPY,   DEFAULT, 0 },
};

// Glare and shiver, wind up backwards, charge forward, then back off
const Step angryCharge[] = {
  { 1000,    0,    0, ANGRY,   DEFAULT, FX_SHIVER },
  {  250,  -40,  -40, ANGRY,   N,       0 },
  {  600,  100,  100, ANGRY,   N,       0 },
  {  600,    0,    0, ANGRY,   DEFAULT, 0 },
  {  700,  -45,  -45, DEFAULT, DEFAULT, FX_BLINK },
};

// Startled: jump back, turn away, sweat and peek
const Step scared[] = {
  {  300,   0,   0, DEFAULT, N,       FX_SWEAT | FX_SHIVER },
  {  800, -75, -75, DEFAULT, N,       FX_SWEAT | FX_SHIVER },
  {  500, -50,  50, DEFAULT, SW,      FX_SWEAT },
  { 1200,   0,   0, TIRED,   SW,      FX_SWEAT },
  {  600,   0,   0, DEFAULT, NE,      FX_SWEAT | FX_CURIOUS },
  {  800,   0,   0, DEFAULT, DEFAULT, FX_BLINK },
};

// Spin fast with eyes dragged the other way, then wobble around confused
const Step dizzySpin[] = {
  { 1500,  90, -90, DEFAULT, W,       0 },
  {  500,   0,   0, TIRED,   DEFAULT, FX_CONFUSED },
  {  500,   0,   0, TIRED,   NW,      FX_CONFUSED },
  {  500,   0,   0, TIRED,   SE,      FX_CONFUSED },
  {  600,  20, -20, TIRED,   DEFAULT, FX_BLINK },
  {  800,   0,   0, DEFAULT, DEFAULT, FX_BLINK },
};

// Shuffle slowly, droop, nod off, then wake with a start
const Step sleepy[] = {
  { 1500,  25,  25, TIRED,   S,       0 },
  {  800,   0,   0, TIRED,   S,       FX_BLINK },
  { 3000,   0,   0, TIRED,   S,       FX_CLOSE },
  {  150, -40, -40, DEFAULT, N,       FX_SHIVER },
  {  800,   0,   0, DEFAULT, DEFAULT, FX_CONFUSED },
};

// Turn left and right quickly, like shaking the head "no"
const Step shakeNo[] = {
  {  400,   0,   0, ANGRY,   DEFAULT, 0 },
  {  150, -70,  70, ANGRY,   W,       0 },
  {  150,  70, -70, ANGRY,   E,       0 },
  {  150, -70,  70, ANGRY,   W,       0 },
  {  150,  70, -70, ANGRY,   E,       0 },
  {  800,   0,   0, ANGRY,   DEFAULT, FX_CONFUSED },
};

// Hop forward and back like a nod, then laugh
const Step hello[] = {
  {  250,  60,  60, HAPPY, N,       0 },
  {  250, -60, -60, HAPPY, S,       0 },
  {  250,  60,  60, HAPPY, N,       0 },
  {  250, -60, -60, HAPPY, S,       0 },
  { 1200,   0,   0, HAPPY, DEFAULT, FX_LAUGH },
};

// Order here = numbers 1-8 in commands and buttons on the web page
// (laptop_control/serial_control.html has the same list, update both)
const Behavior behaviors[] = {
  BEHAVIOR("Hello",   hello),
  BEHAVIOR("Dance",   happyDance),
  BEHAVIOR("Explore", explore),
  BEHAVIOR("Angry",   angryCharge),
  BEHAVIOR("Scared",  scared),
  BEHAVIOR("Dizzy",   dizzySpin),
  BEHAVIOR("Sleepy",  sleepy),
  BEHAVIOR("No!",     shakeNo),
};
const uint8_t behaviorCount = sizeof(behaviors) / sizeof(behaviors[0]);
const Behavior wakeUpBehavior = BEHAVIOR("Wake up", wakeUp);
const Behavior fallAsleepBehavior = BEHAVIOR("Fall asleep", fallAsleep);

// Each wheel alone at full power, forward then back, to check wiring and power ("test" command).
// 3 s per direction, because slow gear motors (16 RPM) only make about a turn in that time.
const Step motorTest[] = {
  { 3000,  100,    0, DEFAULT, W,       0 }, // left forward
  {  800,    0,    0, DEFAULT, W,       0 },
  { 3000, -100,    0, DEFAULT, SW,      0 }, // left back
  {  800,    0,    0, DEFAULT, DEFAULT, 0 },
  { 3000,    0,  100, DEFAULT, E,       0 }, // right forward
  {  800,    0,    0, DEFAULT, E,       0 },
  { 3000,    0, -100, DEFAULT, SE,      0 }, // right back
  {  800,    0,    0, DEFAULT, DEFAULT, 0 },
};
const Behavior motorTestBehavior = BEHAVIOR("Motor test", motorTest);


//*********************************************************************************************
//  STATE
//*********************************************************************************************

enum Mode { MODE_REST, MODE_BEHAVIOR, MODE_MANUAL, MODE_SLEEP };
Mode mode = MODE_REST;

const Behavior *current = nullptr; // running behavior
uint8_t stepIndex = 0;
unsigned long stepStart = 0;
bool sleepAfterBehavior = false;   // go to sleep instead of resting when the behavior ends

bool autoMode = AUTO_MODE_AT_START;
unsigned long nextAutoAt = 0;
unsigned long lastUserAction = 0;  // for falling asleep after SLEEP_AFTER_MIN

uint8_t restMood = DEFAULT;        // mood shown while resting
int manualSpeed = MANUAL_SPEED;    // percent
char manualDir = 0;
unsigned long manualUntil = 0;     // manual drive stops at this time unless refreshed

unsigned long infoUntil = 0;       // an info message is on the OLED until this time
bool infoDrawn = false;


//*********************************************************************************************
//  FACE + WHEELS
//*********************************************************************************************

// Switch the screen from the clock back to the eyes
void leaveSleep() {
  if (mode == MODE_SLEEP) {
    clockScreen.leave();
    Serial.println("Waking up");
  }
}

void applyStep() {
  const Step &s = current->steps[stepIndex];
  stepStart = millis();

  drive.set(s.left, s.right);

  roboEyes.setMood(s.mood);
  if (s.look != KEEP) roboEyes.setPosition(s.look);
  roboEyes.setSweat(s.fx & FX_SWEAT);
  roboEyes.setCuriosity(s.fx & FX_CURIOUS);
  roboEyes.setHFlicker((s.fx & FX_SHIVER) ? ON : OFF, 2);
  if (s.fx & FX_CLOSE) roboEyes.close(); else roboEyes.open();
  if (s.fx & FX_BLINK) roboEyes.blink();
  if (s.fx & FX_LAUGH) roboEyes.anim_laugh();
  if (s.fx & FX_CONFUSED) roboEyes.anim_confused();
}

void startBehavior(const Behavior &b) {
  leaveSleep();
  Serial.printf("Behavior: %s\n", b.name);
  current = &b;
  stepIndex = 0;
  sleepAfterBehavior = false;
  mode = MODE_BEHAVIOR;
  roboEyes.setIdleMode(OFF); // the behavior controls where the eyes look
  applyStep();
}

void goRest() {
  leaveSleep();
  mode = MODE_REST;
  current = nullptr;
  manualDir = 0;
  drive.stop();
  roboEyes.setMood(restMood);
  roboEyes.setPosition(DEFAULT);
  roboEyes.setSweat(OFF);
  roboEyes.setCuriosity(OFF);
  roboEyes.setHFlicker(OFF);
  roboEyes.open();
  roboEyes.setIdleMode(ON, 2, 3); // keep the eyes moving every 2-5 s, also spreads OLED wear
  nextAutoAt = millis() + random(4000, 10000);
}

// Play the fall asleep animation, then show the clock
void goToSleep() {
  startBehavior(fallAsleepBehavior);
  sleepAfterBehavior = true;
}

void enterSleep() {
  Serial.println("Sleeping");
  mode = MODE_SLEEP;
  current = nullptr;
  drive.stop();
  roboEyes.setIdleMode(OFF);
  clockScreen.enter();
}

void toggleSleep() {
  if (mode == MODE_SLEEP) startBehavior(wakeUpBehavior);
  else goToSleep();
}

// Drive by hand for holdMs (repeat the command to keep going), the face looks where it's going.
// dir: 'f' forward, 'b' back, 'l' left, 'r' right
void manualDrive(char dir, unsigned long holdMs) {
  manualUntil = millis() + holdMs;

  int turn = manualSpeed * 4 / 5;
  switch (dir) {
    case 'f': drive.set(manualSpeed, manualSpeed); break;
    case 'b': drive.set(-manualSpeed, -manualSpeed); break;
    case 'l': drive.set(-turn, turn); break;
    case 'r': drive.set(turn, -turn); break;
  }

  if (mode == MODE_MANUAL && dir == manualDir) return; // keep-alive, face is already set

  leaveSleep();
  mode = MODE_MANUAL;
  manualDir = dir;
  current = nullptr;
  roboEyes.setIdleMode(OFF);
  roboEyes.setMood(restMood);
  roboEyes.setHFlicker(OFF);
  roboEyes.open();
  roboEyes.setCuriosity(dir == 'l' || dir == 'r');
  roboEyes.setSweat(dir == 'b'); // nervous when reversing blind
  switch (dir) {
    case 'f': roboEyes.setPosition(N); break;
    case 'b': roboEyes.setPosition(S); break;
    case 'l': roboEyes.setPosition(W); break;
    case 'r': roboEyes.setPosition(E); break;
  }
}

void updateMode() {
  unsigned long now = millis();
  switch (mode) {
    case MODE_BEHAVIOR:
      if (now - stepStart >= current->steps[stepIndex].ms) {
        if (++stepIndex < current->count) applyStep();
        else if (sleepAfterBehavior) enterSleep();
        else goRest();
      }
      break;
    case MODE_MANUAL:
      if ((long)(now - manualUntil) >= 0) goRest(); // no command lately: stop for safety
      break;
    case MODE_REST:
      if (now - lastUserAction >= SLEEP_AFTER_MIN * 60000UL) {
        goToSleep();
      } else if (autoMode && (long)(now - nextAutoAt) >= 0) {
        startBehavior(behaviors[random(behaviorCount)]);
      }
      break;
    case MODE_SLEEP:
      break; // woken up by commands
  }
}


//*********************************************************************************************
//  OLED INFO MESSAGES
//*********************************************************************************************

// Show a few lines of text on the OLED for some seconds, then go back to the eyes/clock
String infoLines[4];

void showInfo(const String &l1, const String &l2, const String &l3, const String &l4, unsigned long ms) {
  infoLines[0] = l1;
  infoLines[1] = l2;
  infoLines[2] = l3;
  infoLines[3] = l4;
  infoUntil = millis() + ms;
  infoDrawn = false;
}

// Returns true while a message is on screen
bool updateInfo() {
  if (infoUntil == 0) return false;
  if ((long)(millis() - infoUntil) >= 0) {
    infoUntil = 0;
    display.clearDisplay();
    if (mode == MODE_SLEEP) clockScreen.enter(); // back to the dimmed clock
    return false;
  }
  if (!infoDrawn) {
    if (mode == MODE_SLEEP) clockScreen.leave(); // full brightness, screen on even at night
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    for (int i = 0; i < 4; i++) {
      display.setCursor(0, 4 + i * 16);
      display.print(infoLines[i]);
    }
    display.display();
    infoDrawn = true;
  }
  return true;
}


//*********************************************************************************************
//  WIFI CONNECTION STATE
//*********************************************************************************************

const char *wifiSsid = HOME_WIFI_SSID;   // network currently being tried
const char *wifiPass = HOME_WIFI_PASS;
unsigned long wifiAttemptAt = 0;         // when that network was first tried
volatile uint8_t wifiFailReason = 0;     // why the last connection attempt failed, 0 = no failure

void onWiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  wifiFailReason = info.wifi_sta_disconnected.reason;
}

const char *wifiReasonText(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
      return "Network not found. The robot only sees 2.4 GHz networks, type wifi to list them.";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_EXPIRE:
      return "Password rejected, check the password in config.h.";
    case WIFI_REASON_BEACON_TIMEOUT:
      return "Signal lost, the robot may be too far from the router.";
    default:
      return "Connection failed.";
  }
}

void beginWiFi(const char *ssid, const char *pass) {
  wifiSsid = ssid;
  wifiPass = pass;
  wifiAttemptAt = millis();
  wifiFailReason = 0;
  WiFi.begin(ssid, pass); // keeps retrying by itself if the connection drops
  Serial.printf("Connecting to WiFi \"%s\"...\n", ssid);
}

// Scan for networks (takes about 3 s) and list them
String wifiScan() {
  bool connected = WiFi.status() == WL_CONNECTED;
  if (!connected) WiFi.disconnect(); // a scan can't run while a connection attempt is going on
  int n = WiFi.scanNetworks();
  String s;
  if (n < 0) {
    s = "WiFi scan failed, try again";
  } else {
    bool foundHome = false;
    s = String(n) + " networks found (the robot only sees 2.4 GHz):";
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid == HOME_WIFI_SSID) foundHome = true;
      s += "\n  " + (ssid.length() ? ssid : String("(hidden)")) + "   " + WiFi.RSSI(i) + " dBm   channel "
         + WiFi.channel(i) + "   " + WiFi.BSSIDstr(i);
    }
    if (strlen(HOME_WIFI_SSID)) {
      s += foundHome ? "\n\"" HOME_WIFI_SSID "\" is in range on 2.4 GHz."
                     : "\n\"" HOME_WIFI_SSID "\" is NOT in the list, so it isn't on 2.4 GHz (or is out of range).";
    }
  }
  WiFi.scanDelete();
  if (!connected && strlen(wifiSsid)) beginWiFi(wifiSsid, wifiPass);
  return s;
}


//*********************************************************************************************
//  COMMANDS - shared by serial (USB) and WiFi
//*********************************************************************************************

const char HELP_TEXT[] =
  "Drive (each command drives a moment, repeat it to keep going):\n"
  "  w / s / a / d          forward / back / left / right\n"
  "  forward 2000           drive for 2000 ms (also back, left, right)\n"
  "  x or stop              stop everything\n"
  "  speed 60               manual speed in percent (30-100)\n"
  "Show:\n"
  "  1-8 or dance, hello... play a behavior (list below)\n"
  "  mood happy             normal / happy / angry / tired\n"
  "  sleep / wake / z       clock screen on / off / toggle\n"
  "  auto on / auto off / m random behaviors by itself\n"
  "Motors:\n"
  "  test                   each wheel forward and back at full power\n"
  "  minpwm 80              lowest motor power 0-255 (raise if wheels hum but don't turn)\n"
  "  maxpwm 255             highest motor power 0-255\n"
  "  kick 80                full-power start pulse in ms, 0 = off\n"
  "Info:\n"
  "  status                 mode, WiFi addresses, weather\n"
  "  wifi                   list the 2.4 GHz networks the robot can see\n"
  "  help or ?              this list\n"
  "Behaviors:";

const char *modeName() {
  switch (mode) {
    case MODE_BEHAVIOR: return current ? current->name : "behavior";
    case MODE_MANUAL: return "driving";
    case MODE_SLEEP: return "sleeping";
    default: return "resting";
  }
}

String homeAddress() {
  return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");
}

String statusText() {
  String s = String("Mode: ") + modeName() + ", auto " + (autoMode ? "on" : "off")
           + ", speed " + manualSpeed + "%\n";
  if (strlen(HOME_WIFI_SSID) == 0) s += "Home WiFi: not set in config.h\n";
  else if (homeAddress().length()) s += "Home WiFi \"" + WiFi.SSID() + "\": http://" + homeAddress() + "  or  http://" HOSTNAME ".local\n";
  else {
    s += String("Home WiFi: connecting to \"") + wifiSsid + "\"...";
    if (wifiFailReason) s += String(" ") + wifiReasonText(wifiFailReason);
    s += "\n";
  }
  if (WiFi.getMode() & WIFI_AP) s += "Robot WiFi \"" AP_SSID "\": http://" + WiFi.softAPIP().toString() + "\n";
  String city = getLocationCity();
  if (city.length()) s += "Location: " + city + " (found from WiFi)\n";
  else s += AUTO_LOCATION ? "Location: config.h until found from WiFi\n" : "Location: from config.h\n";
  Weather w = getWeather();
  if (w.valid) s += String("Weather: ") + String(w.temperature, 1) + (WEATHER_FAHRENHEIT ? "F " : "C ") + weatherText(w.code);
  else s += "Weather: none yet";
  return s;
}

String statusJson() {
  String s = String("{\"mode\":\"") + modeName() + "\",\"auto\":" + (autoMode ? "true" : "false")
           + ",\"speed\":" + manualSpeed + ",\"ip\":\"" + homeAddress() + "\"}";
  return s;
}

// Finds a behavior by number (1-8) or by the start of its name, -1 if none
int findBehavior(const String &name) {
  int n = name.toInt();
  if (n >= 1 && n <= behaviorCount && String(n) == name) return n - 1;
  for (uint8_t i = 0; i < behaviorCount; i++) {
    String b = behaviors[i].name;
    b.toLowerCase();
    if (b.startsWith(name)) return i;
  }
  return -1;
}

// Runs one text command and returns a reply ("" = nothing to say)
String runCommand(String line) {
  line.trim();
  line.toLowerCase();
  if (line.length() == 0) return "";
  lastUserAction = millis();

  int space = line.indexOf(' ');
  String cmd = space < 0 ? line : line.substring(0, space);
  String arg = space < 0 ? "" : line.substring(space + 1);
  arg.trim();

  // Driving
  char dir = 0;
  if (cmd == "w" || cmd == "forward") dir = 'f';
  else if (cmd == "s" || cmd == "back") dir = 'b';
  else if (cmd == "a" || cmd == "left") dir = 'l';
  else if (cmd == "d" || cmd == "right") dir = 'r';
  if (dir) {
    unsigned long ms = arg.length() ? constrain(arg.toInt(), 100, 10000) : MANUAL_TIMEOUT_MS;
    manualDrive(dir, ms);
    return "";
  }

  if (cmd == "x" || cmd == "stop") {
    goRest();
    return "";
  }
  if (cmd == "speed") {
    if (arg.length()) manualSpeed = constrain(arg.toInt(), 30, 100);
    return String("Speed ") + manualSpeed + "%";
  }
  if (cmd == "z") {
    toggleSleep();
    return "";
  }
  if (cmd == "sleep") {
    if (mode != MODE_SLEEP) goToSleep();
    return "";
  }
  if (cmd == "wake") {
    if (mode == MODE_SLEEP) startBehavior(wakeUpBehavior);
    return "";
  }
  if (cmd == "m" || cmd == "auto") {
    if (arg == "on") autoMode = true;
    else if (arg == "off") autoMode = false;
    else autoMode = !autoMode;
    nextAutoAt = millis() + 2000;
    return String("Auto mode ") + (autoMode ? "on" : "off");
  }
  if (cmd == "mood") {
    if (arg == "normal") restMood = DEFAULT;
    else if (arg == "happy") restMood = HAPPY;
    else if (arg == "angry") restMood = ANGRY;
    else if (arg == "tired") restMood = TIRED;
    else return "Moods: normal, happy, angry, tired";
    if (mode == MODE_REST || mode == MODE_MANUAL) roboEyes.setMood(restMood);
    return "Mood " + arg;
  }
  if (cmd == "test") {
    Serial.println("Motor test: left forward, left back, right forward, right back");
    startBehavior(motorTestBehavior);
    return "";
  }
  if (cmd == "minpwm") {
    if (arg.length()) drive.minPwm = constrain(arg.toInt(), 0, 255);
    return String("Min PWM ") + drive.minPwm;
  }
  if (cmd == "maxpwm") {
    if (arg.length()) drive.maxPwm = constrain(arg.toInt(), 0, 255);
    return String("Max PWM ") + drive.maxPwm;
  }
  if (cmd == "kick") {
    if (arg.length()) drive.kickMs = constrain(arg.toInt(), 0, 500);
    return String("Kick ") + drive.kickMs + " ms";
  }
  if (cmd == "wifi") return wifiScan();
  if (cmd == "status" || cmd == "ip") return statusText();
  if (cmd == "help" || cmd == "?") {
    String s = HELP_TEXT;
    for (uint8_t i = 0; i < behaviorCount; i++) s += String("\n  ") + (i + 1) + " " + behaviors[i].name;
    return s;
  }

  int b = findBehavior(cmd == "play" ? arg : line);
  if (b >= 0) {
    startBehavior(behaviors[b]);
    return "";
  }
  return "Unknown command \"" + line + "\", type help";
}


//*********************************************************************************************
//  WEB CONTROL
//*********************************************************************************************

const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>RoboFace</title><style>
body{font-family:system-ui,sans-serif;background:#111;color:#eee;text-align:center;margin:0;padding:12px;user-select:none;-webkit-user-select:none}
h2{margin:8px 0 4px}
#st{color:#9a9a9a;font-size:14px;min-height:20px;margin-bottom:12px}
.pad{display:grid;grid-template-columns:repeat(3,80px);gap:8px;justify-content:center;margin-bottom:14px}
button{font-size:18px;padding:14px 0;border:0;border-radius:12px;background:#2a2a2a;color:#eee;touch-action:none;cursor:pointer}
button:active,button.on{background:#0a84ff}
.stop{background:#a33}
.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;max-width:340px;margin:0 auto 16px}
.moods{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;max-width:340px;margin:0 auto 16px}
.moods button{font-size:14px;padding:10px 0}
.row{max-width:340px;margin:0 auto 16px;display:flex;align-items:center;gap:10px;font-size:16px}
.row input[type=range]{flex:1}
.keys{color:#777;font-size:13px;line-height:1.5}
</style></head><body><h2>RoboFace</h2><div id="st">connecting...</div>
<div class="pad">
<span></span><button data-k="w">&#9650;</button><span></span>
<button data-k="a">&#9664;</button><button class="stop" onclick="stop()">&#9632;</button><button data-k="d">&#9654;</button>
<span></span><button data-k="s">&#9660;</button><span></span>
</div>
<div class="row">Speed<input type="range" id="spd" min="30" max="100" step="5" onchange="cmd('speed '+this.value)"><span id="spv"></span></div>
<div class="grid">)HTML";

const char PAGE_TAIL[] PROGMEM = R"HTML(</div>
<div class="moods">
<button onclick="cmd('mood normal')">Normal</button><button onclick="cmd('mood happy')">Happy</button>
<button onclick="cmd('mood angry')">Angry</button><button onclick="cmd('mood tired')">Tired</button>
</div>
<div class="grid">
<button onclick="cmd('sleep')">&#127769; Sleep</button><button onclick="cmd('wake')">&#9728;&#65039; Wake up</button>
</div>
<div class="row" style="justify-content:center"><label><input type="checkbox" id="auto" onchange="cmd('auto '+(this.checked?'on':'off'))"> Auto mode</label></div>
<div class="keys">Laptop keys: W A S D or arrows to drive (hold), Space stop,<br>1-8 behaviors, Z sleep/wake, M auto mode</div>
<script>
const $=id=>document.getElementById(id);
function cmd(c){return fetch('/cmd?c='+encodeURIComponent(c)).then(r=>r.text()).catch(()=>'');}
let held=null,timer=null;
function hold(k){
  if(held===k)return;
  clearInterval(timer);held=k;cmd(k);timer=setInterval(()=>cmd(k),250);
  document.querySelectorAll('[data-k]').forEach(b=>b.classList.toggle('on',b.dataset.k===k));
}
function release(){
  if(!held)return;
  clearInterval(timer);held=null;cmd('x');
  document.querySelectorAll('[data-k]').forEach(b=>b.classList.remove('on'));
}
function stop(){release();cmd('x');}
document.querySelectorAll('[data-k]').forEach(b=>{
  b.addEventListener('pointerdown',e=>{e.preventDefault();hold(b.dataset.k);});
  ['pointerup','pointerleave','pointercancel'].forEach(ev=>b.addEventListener(ev,()=>{if(held===b.dataset.k)release();}));
});
const keyMap={w:'w',arrowup:'w',s:'s',arrowdown:'s',a:'a',arrowleft:'a',d:'d',arrowright:'d'};
addEventListener('keydown',e=>{
  const k=e.key.toLowerCase();
  if(keyMap[k]){e.preventDefault();if(!e.repeat)hold(keyMap[k]);}
  else if(k===' '||k==='x'){e.preventDefault();stop();}
  else if(/^[1-9]$/.test(k)||k==='z'||k==='m')cmd(k);
});
addEventListener('keyup',e=>{const k=keyMap[e.key.toLowerCase()];if(k&&held===k)release();});
addEventListener('blur',release);
function poll(){
  fetch('/status').then(r=>r.json()).then(s=>{
    $('st').textContent=s.mode+' · auto '+(s.auto?'on':'off')+(s.ip?' · '+s.ip:'');
    $('auto').checked=s.auto;
    if(document.activeElement!==$('spd')){$('spd').value=s.speed;}
    $('spv').textContent=$('spd').value+'%';
  }).catch(()=>{$('st').textContent='offline';});
}
$('spd').addEventListener('input',()=>{$('spv').textContent=$('spd').value+'%';});
poll();setInterval(poll,2000);
</script></body></html>)HTML";

void handleRoot() {
  String html = PAGE_HEAD;
  for (uint8_t i = 0; i < behaviorCount; i++) {
    html += "<button onclick=\"cmd('" + String(i + 1) + "')\">" + behaviors[i].name + "</button>";
  }
  html += PAGE_TAIL;
  server.send(200, "text/html", html);
}

void handleCmd() {
  String reply = runCommand(server.arg("c"));
  server.send(200, "text/plain", reply.length() ? reply : "ok");
}

void handleStatus() {
  server.send(200, "application/json", statusJson());
}


//*********************************************************************************************
//  WIFI
//*********************************************************************************************

void setupWiFi() {
  bool home = strlen(HOME_WIFI_SSID) > 0;
  bool ap = ROBOT_AP_ENABLED || !home; // without home WiFi the robot's own network is the only way in

  WiFi.setHostname(HOSTNAME);
  WiFi.mode(ap && home ? WIFI_AP_STA : (ap ? WIFI_AP : WIFI_STA));

  if (ap) {
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print("Robot WiFi \"" AP_SSID "\": http://");
    Serial.println(WiFi.softAPIP());
  }

  if (home) {
    WiFi.onEvent(onWiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    beginWiFi(HOME_WIFI_SSID, HOME_WIFI_PASS);
    configTzTime(TIME_ZONE, "pool.ntp.org", "time.nist.gov"); // clock syncs once connected
    startWeather();
  } else {
    Serial.println("No home WiFi set in config.h: clock and weather are off");
  }

  // http://roboface.local on the home network
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);

  server.on("/", handleRoot);
  server.on("/cmd", handleCmd);
  server.on("/status", handleStatus);
  server.begin();
}

// When the home WiFi connects, show the address on serial and on the OLED for a few seconds.
// Reports why a connection fails, and switches between the home and backup network if needed.
void checkWiFi() {
  static bool wasConnected = false;
  static uint8_t reportedReason = 0;
  bool connected = WiFi.status() == WL_CONNECTED;

  if (connected && !wasConnected) {
    String ip = WiFi.localIP().toString();
    Serial.println("WiFi \"" + WiFi.SSID() + "\" connected: http://" + ip + "  or  http://" HOSTNAME ".local");
    showInfo("WiFi connected!", "Open in a browser:", "http://" + ip, HOSTNAME ".local", 6000);
    wifiFailReason = 0;
  }
  wasConnected = connected;
  if (connected || strlen(HOME_WIFI_SSID) == 0) return;

  // Explain a failure once instead of every retry
  uint8_t reason = wifiFailReason;
  if (reason && reason != reportedReason) {
    Serial.printf("WiFi \"%s\": %s (code %d)\n", wifiSsid, wifiReasonText(reason), reason);
  }
  reportedReason = reason;

  // Try the other network after 20 s without success
  if (strlen(BACKUP_WIFI_SSID) && millis() - wifiAttemptAt > 20000) {
    WiFi.disconnect();
    if (strcmp(wifiSsid, HOME_WIFI_SSID) == 0) beginWiFi(BACKUP_WIFI_SSID, BACKUP_WIFI_PASS);
    else beginWiFi(HOME_WIFI_SSID, HOME_WIFI_PASS);
  }
}


//*********************************************************************************************
//  SERIAL CONTROL
//*********************************************************************************************

void runSerialLine(String &line) {
  String reply = runCommand(line);
  if (reply.length()) Serial.println(reply);
  line = "";
}

// One command per line. Also works with the Serial Monitor set to "No line ending":
// the command runs as soon as no more characters arrive.
void handleSerial() {
  static String line;
  static unsigned long lastChar = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') runSerialLine(line);
    else if (line.length() < 64) line += c;
    lastChar = millis();
  }
  if (line.length() && millis() - lastChar > 100) runSerialLine(line);
}


//*********************************************************************************************
//  SETUP / LOOP
//*********************************************************************************************

void setup() {
  drive.begin(); // first, so the motors are off as early as possible
  Serial.begin(115200);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
    for (;;);
  }

  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 50); // screen size, max fps
  roboEyes.setAutoblinker(ON, 3, 2); // blink every 3-5 s

  setupWiFi();
  Serial.println("Type help for commands");
  lastUserAction = millis();
  startBehavior(wakeUpBehavior);
}

void loop() {
  server.handleClient();
  handleSerial();
  checkWiFi();
  applyAutoTimeZone();
  updateMode();
  drive.update();
  if (updateInfo()) return;  // message on screen
  if (mode == MODE_SLEEP) clockScreen.update();
  else roboEyes.update(); // never use long delay()s, the eyes and motor ramps need this to run often
}

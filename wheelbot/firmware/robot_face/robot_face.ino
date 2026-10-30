  // Robot face + wheels for Seeed XIAO ESP32-C3
//
// Hardware
//   OLED ELEGOO 0.96" SSD1306 128x64 (I2C): SDA -> D4 (GPIO6), SCL -> D5 (GPIO7)
//   DRV8833:     IN1 -> A0 (GPIO2), IN2 -> A1 (GPIO3)   left motor  (OUT1/OUT2)
//                IN3 -> A2 (GPIO4), IN4 -> A3 (GPIO5)   right motor (OUT3/OUT4)
//                1 kOhm on each IN wire, EEP -> 3V3, GND shared with the XIAO GND
//
// Arduino IDE: Board "XIAO_ESP32C3", USB CDC On Boot "Enabled",
//              Partition Scheme "Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)" (for updates over WiFi)
//              After flashing once over USB, later updates can go over WiFi: Tools > Port > roboface
// Libraries (Arduino Library Manager): Adafruit SSD1306 (also installs Adafruit GFX)
// Files in this folder: config.h (your settings), motors.h, weather.h, clock_screen.h,
//                       FluxGarage_RoboEyes.h
//
// Control (all use the same text commands, type "help" for the list)
//   Same WiFi:  http://roboface.local or the IP shown on the OLED, from a phone or laptop browser.
//               The page has a joystick; add it to the phone's home screen to use it like an app
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
#include <ArduinoOTA.h>
#include <esp_ota_ops.h>
#include <Preferences.h>
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
            MOTOR_MIN_PWM, MOTOR_MAX_PWM, MOTOR_KICK_MS, MOTOR_PWM_HZ, MOTOR_SLOW_DECAY);
WebServer server(80);
Preferences prefs; // alarm, reminders and the recorded dance survive a restart


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

// Personality, played by itself while resting (see PERSONALITY)
const int8_t wiggle = BORED_WIGGLE ? 35 : 0;

// Alone for a while: look up at you, wiggle, sigh
const Step bored[] = {
  {  700,       0,       0, DEFAULT, N,       FX_CURIOUS },
  {  200, -wiggle,  wiggle, DEFAULT, NW,      0 },
  {  200,  wiggle, -wiggle, DEFAULT, NE,      0 },
  {  200, -wiggle,  wiggle, DEFAULT, NW,      0 },
  {  200,  wiggle, -wiggle, DEFAULT, NE,      0 },
  { 1000,       0,       0, TIRED,   S,       FX_BLINK },
  {  600,       0,       0, DEFAULT, N,       0 },
};

// Cold outside: shiver
const Step shiver[] = {
  { 1500,   0,   0, DEFAULT, KEEP,    FX_SHIVER },
  {  500,   0,   0, DEFAULT, KEEP,    FX_BLINK },
};

// Late evening: yawn
const Step yawn[] = {
  {  700,   0,   0, TIRED,   N,       0 },
  { 1300,   0,   0, TIRED,   N,       FX_CLOSE },
  {  700,   0,   0, TIRED,   DEFAULT, FX_BLINK },
};

// A message or reminder arrived: look up surprised, small wiggle, smile
const Step gotNote[] = {
  {  500,       0,       0, DEFAULT, N,       FX_CURIOUS },
  {  200, -wiggle,  wiggle, HAPPY,   N,       0 },
  {  200,  wiggle, -wiggle, HAPPY,   N,       0 },
  { 1200,       0,       0, HAPPY,   DEFAULT, FX_LAUGH },
};

const Behavior boredBehavior = BEHAVIOR("Bored", bored);
const Behavior shiverBehavior = BEHAVIOR("Shiver", shiver);
const Behavior yawnBehavior = BEHAVIOR("Yawn", yawn);
const Behavior gotNoteBehavior = BEHAVIOR("Got a note", gotNote);

// The dance recorded from the phone ("rec"), number 9
#define REC_MAX_STEPS 160
Step recSteps[REC_MAX_STEPS];
Behavior myDance = { "My dance", recSteps, 0 };


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
int manualLook = -1;               // where the eyes look while driving, -1 = not driving
unsigned long manualUntil = 0;     // manual drive stops at this time unless refreshed

unsigned long infoUntil = 0;       // an info message is on the OLED until this time
bool infoDrawn = false;
bool infoUntilWeb = false;         // the message also closes as soon as the web page is used
bool webUsed = false;              // a phone or laptop is using the web page
unsigned long lastWebAt = 0;       // last request from the web page

bool moodAuto = true;              // the resting mood follows the time of day and the weather

// Note on the OLED (message, reminder, alarm, timer), shown in turns with the eyes until seen
String noteText;                   // "" = no note. Character 3 is a heart
const char *noteKind = "";         // "Message", "Reminder", "Alarm" or "Timer"
unsigned long noteUntil = 0;       // the note disappears by itself at this time, 0 = only when seen

// Focus timer
enum FocusPhase { FOCUS_OFF, FOCUS_WORK, FOCUS_BREAK };
FocusPhase focusPhase = FOCUS_OFF;
unsigned long focusEndAt = 0;

// Daily alarm
int alarmMinute = -1;              // minutes after midnight, -1 = off
bool alarmRinging = false;
unsigned long ringUntil = 0;

// Repeating reminders
struct Reminder {
  char text[24];
  uint16_t everyMin;               // 0 = unused slot
};
#define REMINDER_COUNT 4
Reminder reminders[REMINDER_COUNT];
unsigned long reminderDueAt[REMINDER_COUNT];

// Dance recording
bool recording = false;
bool recHasSegment = false;        // a drive segment is being timed
unsigned long recStartedAt = 0, recSegmentStart = 0;
int8_t recLeft = 0, recRight = 0;
uint8_t recLook = DEFAULT;

// Robot WiFi power saving
bool apOn = false;                 // the robot's own network is on
bool apKeepOn = false;             // "ap on": don't switch it off by itself
bool homePaused = false;           // home WiFi attempts paused while a phone is on the robot WiFi

unsigned long loopMaxMs = 0;       // slowest loop in the last few seconds, shown by "status"

extern const char *wifiSsid;       // home network being tried, see WIFI CONNECTION STATE
extern const char *wifiPass;


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
  if (manualLook >= 0) recordDrive(0, 0, DEFAULT);
  manualLook = -1;
  drive.stop();
  applyRestFace();
  roboEyes.setPosition(DEFAULT);
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
  struct tm t;
  if (!ClockScreen::readTime(t)) Serial.println("Sleeping, but the clock isn't set yet: open the phone page once");
  else if (ClockScreen::isNight(t.tm_hour)) Serial.printf(NIGHT_SMALL_CLOCK ? "Sleeping, small night clock (%d:00-%d:00)\n"
                                                                            : "Sleeping, screen off for the night (%d:00-%d:00)\n",
                                                          NIGHT_OFF_START, NIGHT_OFF_END);
  else Serial.println("Sleeping");
  // The clock wins over a WiFi screen still waiting for a phone
  infoUntil = 0;
  infoUntilWeb = false;
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
// left / right: wheel speeds in percent, look: eye position (N, NE, E ... or DEFAULT)
void manualDrive(int left, int right, int look, unsigned long holdMs) {
  manualUntil = millis() + holdMs;
  drive.set(left, right);
  recordDrive(left, right, look);

  if (mode == MODE_MANUAL && look == manualLook) return; // keep-alive, face is already set

  leaveSleep();
  mode = MODE_MANUAL;
  manualLook = look;
  current = nullptr;
  roboEyes.setIdleMode(OFF);
  roboEyes.setMood(restFaceMood());
  roboEyes.setHFlicker(OFF);
  roboEyes.open();
  roboEyes.setCuriosity(look == E || look == W);
  roboEyes.setSweat(look == S || look == SE || look == SW); // nervous when reversing blind
  roboEyes.setPosition(look);
}

// Arrow key style driving. dir: 'f' forward, 'b' back, 'l' left, 'r' right
void manualDrive(char dir, unsigned long holdMs) {
  int turn = manualSpeed * 4 / 5;
  switch (dir) {
    case 'f': manualDrive(manualSpeed, manualSpeed, N, holdMs); break;
    case 'b': manualDrive(-manualSpeed, -manualSpeed, S, holdMs); break;
    case 'l': manualDrive(-turn, turn, W, holdMs); break;
    case 'r': manualDrive(turn, -turn, E, holdMs); break;
  }
}

// Joystick driving from the phone page. x: -100 (left) .. 100 (right), y: -100 (back) .. 100 (forward).
// Mixes both into wheel speeds, so pushing up and a bit right drives a curve.
void joystickDrive(int x, int y, unsigned long holdMs) {
  x = constrain(x, -100, 100) * 4 / 5; // turn a bit slower than straight, like the arrow keys
  y = constrain(y, -100, 100);
  int left = y + x, right = y - x;
  int biggest = max(abs(left), abs(right));
  if (biggest > 100) {
    left = left * 100 / biggest;
    right = right * 100 / biggest;
  }

  // Eyes look towards the stick, split into 8 directions
  int look = DEFAULT;
  if (x != 0 || y != 0) {
    const int dirs[8] = { E, NE, N, NW, W, SW, S, SE };
    float angle = atan2((float)y, (float)x); // 0 = right, counter-clockwise
    int sector = (int)lround(angle / (PI / 4)) & 7;
    look = dirs[sector];
  }
  manualDrive(left * manualSpeed / 100, right * manualSpeed / 100, look, holdMs);
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
      if (focusPhase == FOCUS_WORK || alarmRinging || recording) {
        break; // stay awake and quiet: focusing, ringing, or recording a dance
      } else if (now - lastUserAction >= SLEEP_AFTER_MIN * 60000UL) {
        goToSleep();
      } else if (autoMode && (long)(now - nextAutoAt) >= 0) {
        startBehavior(behaviors[random(behaviorCount)]);
      } else if (!autoMode) {
        updatePersonality();
      }
      break;
    case MODE_SLEEP:
      break; // woken up by commands
  }
}


//*********************************************************************************************
//  PERSONALITY - with "mood auto" (the default) the face follows the time of day and weather
//*********************************************************************************************

// Mood for resting and driving: the one picked with "mood", or with "mood auto" happy in the
// morning, tired late in the evening and at night
uint8_t restFaceMood() {
  if (!moodAuto) return restMood;
  struct tm t;
  if (!ClockScreen::readTime(t)) return DEFAULT;
  int h = t.tm_hour;
  if (h >= MORNING_START && h < MORNING_END) return HAPPY;
  if (h >= NIGHT_TIRED_START || h < MORNING_START) return TIRED;
  return DEFAULT;
}

bool hotOutside() {
  Weather w = getWeather();
  return moodAuto && w.valid && w.temperature >= HOT_TEMP;
}

bool coldOutside() {
  Weather w = getWeather();
  return moodAuto && w.valid && w.temperature <= COLD_TEMP;
}

void applyRestFace() {
  roboEyes.setMood(restFaceMood());
  roboEyes.setSweat(hotOutside() ? ON : OFF);
}

// While resting: refresh the face now and then, and every 1-2 minutes do something by itself:
// ask for attention when nobody played for BORED_AFTER_MIN, shiver when it's cold, yawn at night
void updatePersonality() {
  static unsigned long nextFaceAt = 0, nextActAt = 0;
  unsigned long now = millis();
  if ((long)(now - nextFaceAt) >= 0) {
    applyRestFace(); // the hour or the weather may have changed
    nextFaceAt = now + 30000;
  }
  if (!moodAuto) return;
  if (nextActAt == 0) nextActAt = now + 60000;
  if ((long)(now - nextActAt) < 0) return;
  nextActAt = now + random(60000, 120000);

  bool alone = BORED_AFTER_MIN > 0 && now - lastUserAction >= BORED_AFTER_MIN * 60000UL;
  if (alone) startBehavior(boredBehavior);
  else if (coldOutside()) startBehavior(shiverBehavior);
  else if (restFaceMood() == TIRED) startBehavior(yawnBehavior);
}


//*********************************************************************************************
//  NOTES - messages from the phone, reminders, alarm and timer texts on the OLED
//*********************************************************************************************

// Text from the phone for the OLED font: hearts become character 3, curly quotes straight ones,
// other emoji and accents are dropped
String cleanNoteText(String s) {
  s.replace("\xE2\x9D\xA4\xEF\xB8\x8F", "\x03"); // red heart emoji
  s.replace("\xE2\x9D\xA4", "\x03");             // heart without the color selector
  s.replace("<3", "\x03");
  s.replace("\xE2\x80\x98", "'");
  s.replace("\xE2\x80\x99", "'");
  s.replace("\xE2\x80\x9C", "\"");
  s.replace("\xE2\x80\x9D", "\"");
  String out;
  for (unsigned int i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (c == 3 || (c >= 32 && c < 127)) out += (char)c;
  }
  out.trim();
  if (out.length() > NOTE_MAX_LEN) out = out.substring(0, NOTE_MAX_LEN);
  return out;
}

// Put a note on the OLED. showMin = 0: it stays until "seen". The robot reacts, and wakes up
// for it unless it's night.
void showNote(const char *kind, const String &text, unsigned long showMin) {
  noteKind = kind;
  noteText = text;
  noteUntil = showMin ? millis() + showMin * 60000UL : 0;
  clockScreen.noteWaiting = true;
  Serial.printf("%s: %s\n", kind, text.c_str());

  struct tm t;
  bool night = mode == MODE_SLEEP && ClockScreen::readTime(t) && ClockScreen::isNight(t.tm_hour);
  if (!night && mode != MODE_MANUAL) startBehavior(gotNoteBehavior);
}

void clearNote() {
  noteText = "";
  noteKind = "";
  noteUntil = 0;
  clockScreen.noteWaiting = false;
}

// Split text into lines of at most width characters, breaking at spaces. Returns the line count.
uint8_t wrapText(const String &text, uint8_t width, String *lines, uint8_t maxLines) {
  uint8_t n = 0;
  int start = 0, len = text.length();
  while (start < len && n < maxLines) {
    while (start < len && text[start] == ' ') start++;
    if (start >= len) break;
    if (start + width >= len) {
      lines[n++] = text.substring(start);
      break;
    }
    int cut = text.lastIndexOf(' ', start + width);
    if (cut <= start) cut = start + width; // one long word: cut it
    lines[n++] = text.substring(start, cut);
    start = cut;
  }
  return n;
}

// A card: the title in the top (yellow) rows, the text below, in big letters when it's short.
// shift (0-3) moves everything a few pixels against burn-in.
void drawCard(const String &title, const String &body, uint8_t shift) {
  int ox = (shift & 1) * 2, oy = (shift >> 1) * 2;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.setTextSize(1);
  display.setCursor(ox, 4 + oy);
  display.print(title);

  String lines[5];
  uint8_t n = wrapText(body, 10, lines, 3);
  if (n <= 2) { // fits in big letters, centered
    display.setTextSize(2);
    for (uint8_t i = 0; i < n; i++) {
      display.setCursor((SCREEN_WIDTH - (int)lines[i].length() * 12) / 2 + ox, (n == 1 ? 32 : 22 + i * 20) + oy);
      display.print(lines[i]);
    }
  } else {
    n = wrapText(body, 21, lines, 5);
    for (uint8_t i = 0; i < n; i++) {
      display.setCursor(ox, 18 + i * 9 + oy);
      display.print(lines[i]);
    }
  }
  display.display();
}

String focusLeftText() {
  long secs = (long)(focusEndAt - millis()) / 1000;
  if (secs < 0) secs = 0;
  if (secs >= 60) return String((secs + 59) / 60) + " min left";
  return String(secs) + " s left";
}

// While resting, shows a waiting note (7 of every 12 s) and the focus timer (4 of every 30 s)
// in turns with the eyes. Returns true while a card is on screen.
bool updateCard() {
  static bool shown = false;
  static String drawnKey;
  unsigned long now = millis();
  if (noteText.length() && noteUntil && (long)(now - noteUntil) >= 0) clearNote(); // expired

  String title, body;
  if (mode == MODE_REST) {
    if (noteText.length() && (now / 1000) % 12 < 7) {
      title = noteKind;
      body = noteText;
    } else if (focusPhase != FOCUS_OFF && (now / 1000) % 30 < 4) {
      title = focusPhase == FOCUS_WORK ? "Focus" : "Break";
      body = focusLeftText();
    }
  }
  if (!body.length()) {
    shown = false;
    return false;
  }
  uint8_t shift = (now / (PIXEL_SHIFT_SEC * 1000UL)) % 4;
  String key = title + body + shift;
  if (!shown || key != drawnKey) {
    drawCard(title, body, shift);
    drawnKey = key;
    shown = true;
  }
  return true;
}


//*********************************************************************************************
//  TIMERS - focus timer, daily alarm, repeating reminders
//*********************************************************************************************

void saveTimers() {
  prefs.putInt("alarm", alarmMinute);
  prefs.putBytes("remind", reminders, sizeof(reminders));
  clockScreen.alarmSet = alarmMinute >= 0;
}

// "7:30", "07:30", "19:45", "7:30pm", "7pm" -> minutes after midnight, -1 if not a time
int parseClockTime(String s) {
  s.replace(" ", "");
  bool pm = s.endsWith("pm"), am = s.endsWith("am");
  if (pm || am) s.remove(s.length() - 2);
  if (!s.length() || !isDigit(s[0])) return -1;
  int colon = s.indexOf(':');
  int h = s.toInt();
  int m = colon >= 0 ? s.substring(colon + 1).toInt() : 0;
  if (am || pm) {
    if (h < 1 || h > 12) return -1;
    h = h % 12 + (pm ? 12 : 0);
  }
  if (h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

String clockText(int minute) {
  int h = minute / 60, m = minute % 60;
  char buf[12];
  if (CLOCK_24H) snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
  else snprintf(buf, sizeof(buf), "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "AM" : "PM");
  return buf;
}

void startFocus(int minutes) {
  focusPhase = FOCUS_WORK;
  focusEndAt = millis() + minutes * 60000UL;
  if (mode == MODE_SLEEP) startBehavior(wakeUpBehavior);
  Serial.printf("Focus timer: %d min\n", minutes);
}

void ringAlarm() {
  alarmRinging = true;
  ringUntil = millis() + ALARM_RING_MIN * 60000UL;
  struct tm t;
  ClockScreen::readTime(t);
  showNote("Alarm", String(t.tm_hour < 12 ? "Good morning! It's " : "It's ") + clockText(t.tm_hour * 60 + t.tm_min), 0);
  startBehavior(behaviors[1]); // dance
}

void stopAlarm() {
  if (!alarmRinging) return;
  alarmRinging = false;
  if (strcmp(noteKind, "Alarm") == 0) clearNote();
  Serial.println("Alarm stopped");
}

String reminderList() {
  String s;
  for (uint8_t i = 0; i < REMINDER_COUNT; i++) {
    if (!reminders[i].everyMin) continue;
    long left = (long)(reminderDueAt[i] - millis()) / 60000 + 1;
    s += String(s.length() ? "\n" : "") + "Every " + reminders[i].everyMin + " min: " + reminders[i].text
       + " (next in " + left + " min)";
  }
  return s.length() ? s : "No reminders. Use: remind 60 Drink water";
}

String setReminder(int minutes, String text) {
  text = cleanNoteText(text);
  if (text.length() >= sizeof(reminders[0].text)) text = text.substring(0, sizeof(reminders[0].text) - 1);
  if (!text.length()) return "Use: remind 60 Drink water";
  int slot = -1;
  for (uint8_t i = 0; i < REMINDER_COUNT && slot < 0; i++) {
    if (reminders[i].everyMin && text.equalsIgnoreCase(reminders[i].text)) slot = i; // change it
  }
  for (uint8_t i = 0; i < REMINDER_COUNT && slot < 0; i++) {
    if (!reminders[i].everyMin) slot = i;
  }
  if (slot < 0) return "All 4 reminders are used, remove one with: remind off <text>";
  strlcpy(reminders[slot].text, text.c_str(), sizeof(reminders[slot].text));
  reminders[slot].everyMin = minutes;
  reminderDueAt[slot] = millis() + minutes * 60000UL;
  saveTimers();
  return "Reminder every " + String(minutes) + " min: " + text;
}

// Removes the reminder with this text, or all of them when text is empty
String removeReminder(String text) {
  text.trim();
  uint8_t removed = 0;
  for (uint8_t i = 0; i < REMINDER_COUNT; i++) {
    if (reminders[i].everyMin && (!text.length() || text.equalsIgnoreCase(reminders[i].text))) {
      reminders[i].everyMin = 0;
      removed++;
    }
  }
  saveTimers();
  if (!text.length()) return "All reminders off";
  return removed ? "Reminder off: " + text : "No reminder \"" + text + "\", see: remind list";
}

// Checks the timers once a second
void updateTimers() {
  static unsigned long lastCheck = 0, nextRingDanceAt = 0;
  static int lastRingDay = -1;
  unsigned long now = millis();
  if (now - lastCheck < 1000) return;
  lastCheck = now;

  // Focus timer: dance when it's done, then a break
  if (focusPhase != FOCUS_OFF && (long)(now - focusEndAt) >= 0) {
    if (focusPhase == FOCUS_WORK) {
      focusPhase = FOCUS_BREAK;
      focusEndAt = now + FOCUS_BREAK_MIN * 60000UL;
      showNote("Timer", "Well done! Take a " + String(FOCUS_BREAK_MIN) + " min break", FOCUS_BREAK_MIN);
      startBehavior(behaviors[1]); // dance
    } else {
      focusPhase = FOCUS_OFF;
      showNote("Timer", "Break over. Another round?", REMINDER_SHOW_MIN);
    }
  }

  // Daily alarm: dances every 20 s until stopped or ALARM_RING_MIN is over
  struct tm t;
  bool haveTime = ClockScreen::readTime(t);
  if (alarmMinute >= 0 && haveTime && t.tm_hour * 60 + t.tm_min == alarmMinute && t.tm_yday != lastRingDay) {
    lastRingDay = t.tm_yday;
    ringAlarm();
    nextRingDanceAt = now + 20000;
  }
  if (alarmRinging) {
    if ((long)(now - ringUntil) >= 0) {
      stopAlarm();
    } else if (mode == MODE_REST && (long)(now - nextRingDanceAt) >= 0) {
      startBehavior(random(2) ? behaviors[1] : behaviors[0]); // dance or hello
      nextRingDanceAt = now + 20000;
    }
  }

  // Reminders, skipped at night and while the alarm rings
  for (uint8_t i = 0; i < REMINDER_COUNT; i++) {
    if (!reminders[i].everyMin || (long)(now - reminderDueAt[i]) < 0) continue;
    reminderDueAt[i] = now + reminders[i].everyMin * 60000UL;
    bool night = haveTime && ClockScreen::isNight(t.tm_hour);
    if (!night && !alarmRinging) showNote("Reminder", reminders[i].text, REMINDER_SHOW_MIN);
  }

  if (recording && now - recStartedAt >= RECORD_MAX_SEC * 1000UL) Serial.println(stopRecording());
}


//*********************************************************************************************
//  DANCE RECORDING - drive with any controller while recording, played back as dance 9
//*********************************************************************************************

void loadDance() {
  size_t n = prefs.isKey("dance") ? prefs.getBytesLength("dance") : 0;
  if (n && n % sizeof(Step) == 0 && n <= sizeof(recSteps)) myDance.count = prefs.getBytes("dance", recSteps, n) / sizeof(Step);
  else myDance.count = 0;
}

float danceSeconds() {
  unsigned long ms = 0;
  for (uint8_t i = 0; i < myDance.count; i++) ms += recSteps[i].ms;
  return ms / 1000.0;
}

void recPushStep(unsigned long now) {
  if (myDance.count >= REC_MAX_STEPS - 1) return; // the last slot is for the final smile
  unsigned long ms = min(now - recSegmentStart, 5000UL); // long pauses get shortened
  recSteps[myDance.count++] = { (uint16_t)ms, recLeft, recRight, HAPPY, recLook, 0 };
}

// Called with every manual drive while recording. A new step starts when the wheels or the eyes
// change. Speeds are rounded to 10 % so small joystick wobbles don't count, and changes quicker
// than 120 ms are merged into the step.
void recordDrive(int left, int right, int look) {
  if (!recording) return;
  left = (left + (left >= 0 ? 5 : -5)) / 10 * 10;
  right = (right + (right >= 0 ? 5 : -5)) / 10 * 10;
  unsigned long now = millis();
  if (!recHasSegment) {
    if (left == 0 && right == 0) return; // the recording starts with the first move
    recHasSegment = true;
  } else if (left == recLeft && right == recRight && look == recLook) {
    return;
  } else if (now - recSegmentStart >= 120) {
    recPushStep(now);
  } else {
    recLeft = left; // quick change: just update the step that is running
    recRight = right;
    recLook = look;
    return;
  }
  recSegmentStart = now;
  recLeft = left;
  recRight = right;
  recLook = look;
}

String startRecording() {
  if (recording) return "Already recording";
  if (mode == MODE_BEHAVIOR) goRest();
  recording = true;
  recHasSegment = false;
  myDance.count = 0;
  recStartedAt = millis();
  showInfo("Recording...", "Drive with the", "joystick, then", "press Stop & save", 2500);
  return "Recording: drive with the joystick, then \"rec stop\" (at most " + String(RECORD_MAX_SEC) + " s)";
}

String stopRecording() {
  if (!recording) return "Not recording";
  recording = false;
  if (recHasSegment && (recLeft || recRight)) recPushStep(millis());
  while (myDance.count && recSteps[myDance.count - 1].left == 0 && recSteps[myDance.count - 1].right == 0) {
    myDance.count--; // drop the pause at the end
  }
  if (myDance.count == 0) {
    loadDance(); // keep the dance saved before
    return "Nothing recorded: drive with the joystick while recording";
  }
  recSteps[myDance.count++] = { 1000, 0, 0, HAPPY, DEFAULT, FX_LAUGH };
  prefs.putBytes("dance", recSteps, myDance.count * sizeof(Step));
  return "Saved My dance (" + String(danceSeconds(), 1) + " s). Play it with 9";
}

String playMyDance() {
  if (recording) return "Stop the recording first";
  if (myDance.count == 0) return "No dance recorded yet, start one with: rec";
  startBehavior(myDance);
  return "";
}


//*********************************************************************************************
//  POWER SAVING
//*********************************************************************************************

void setAp(bool on) {
  if (on == apOn) return;
  apOn = on;
  if (on) {
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.println("Robot WiFi \"" AP_SSID "\" on: http://" + WiFi.softAPIP().toString());
  } else {
    WiFi.softAPdisconnect(true);
    Serial.println("Robot WiFi off to save power, the home WiFi works. Turn it on with: ap on");
  }
}

// - Home WiFi attempts pause while a phone is on the robot WiFi: each attempt scans all channels,
//   which takes the radio away from the phone and makes driving laggy.
// - The robot WiFi switches off while the home WiFi works and no phone used it for a while, and
//   comes back when the home WiFi is lost.
// - The radio naps between packets (modem sleep, only possible without the robot WiFi) while
//   nobody has the web page open, and the weather waits while someone is driving.
void updatePower() {
  static unsigned long lastCheck = 0, lastPhoneOnAp = 0, homeLostAt = 0;
  static bool radioNaps = true;
  unsigned long now = millis();
  if (now - lastCheck < 500) return;
  lastCheck = now;

  bool home = WiFi.status() == WL_CONNECTED;
  uint8_t phones = apOn ? WiFi.softAPgetStationNum() : 0;
  if (phones) lastPhoneOnAp = now;

  bool pause = !home && phones > 0 && strlen(HOME_WIFI_SSID) > 0;
  if (pause != homePaused) {
    homePaused = pause;
    if (pause) {
      WiFi.disconnect();
      Serial.println("Home WiFi paused while a phone is on the robot WiFi (keeps driving smooth)");
    } else {
      Serial.println("Home WiFi: trying again");
      beginWiFi(wifiSsid, wifiPass);
    }
  }

  if (home) homeLostAt = 0;
  else if (!homeLostAt) homeLostAt = now;
  if (AP_OFF_WHEN_HOME && apOn && !apKeepOn && home && now - lastPhoneOnAp >= AP_OFF_AFTER_MIN * 60000UL) {
    setAp(false);
  }
  if (!apOn && ROBOT_AP_ENABLED && homeLostAt && now - homeLostAt >= 15000) setAp(true);

  bool naps = !webUsed;
  if (naps != radioNaps) {
    WiFi.setSleep(naps);
    radioNaps = naps;
  }
  weatherHold = now - lastUserAction < 30000;
}

// Slowest loop() in the last 5 s, a slow loop makes the web page feel laggy
void trackLoopTime(unsigned long start) {
  static unsigned long windowStart = 0, windowMax = 0;
  unsigned long now = millis();
  windowMax = max(windowMax, now - start);
  if (now - windowStart >= 5000) {
    loopMaxMs = windowMax;
    windowMax = 0;
    windowStart = now;
  }
}


//*********************************************************************************************
//  UPDATES OVER WIFI (OTA)
//*********************************************************************************************

void drawUpdateProgress(int percent) {
  display.ssd1306_command(SSD1306_DISPLAYON);
  display.dim(false);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 4);
  display.print("Updating...");
  display.setCursor(0, 22);
  display.print("Don't turn it off");
  display.drawRect(4, 40, 120, 12, SSD1306_WHITE);
  display.fillRect(6, 42, 116 * percent / 100, 8, SSD1306_WHITE);
  display.display();
}

void setupOTA() {
#if OTA_ENABLED
  if (esp_ota_get_next_update_partition(nullptr) == nullptr) {
    Serial.println("Updates over WiFi are off: flash once over USB with Partition Scheme "
                   "\"Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)\"");
    return;
  }
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.setMdnsEnabled(false); // mDNS already runs, see setupWiFi()
  ArduinoOTA.onStart([]() {
    drive.halt();
    Serial.println("Update over WiFi started");
    drawUpdateProgress(0);
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    static int shown = -1;
    int percent = total ? done * 100UL / total : 0;
    if (percent / 5 != shown) {
      shown = percent / 5;
      drawUpdateProgress(percent);
    }
  });
  ArduinoOTA.onEnd([]() { Serial.println("Update done, restarting"); });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Update failed (error %u)\n", error);
    showInfo("Update failed", "Try again, or", "upload over USB", "", 5000);
  });
  ArduinoOTA.begin();
  MDNS.enableArduino(3232, true);
  Serial.println("Updates over WiFi: in Arduino IDE pick Tools > Port > " HOSTNAME);
#endif
}


//*********************************************************************************************
//  OLED INFO MESSAGES
//*********************************************************************************************

// Show a few lines of text on the OLED for some seconds, then go back to the eyes/clock.
// An optional second page (showInfoAlt) takes turns with the first every INFO_PAGE_MS.
const unsigned long INFO_PAGE_MS = 4000;
String infoLines[4];
String infoAltLines[4];
bool infoHasAlt = false;

void showInfo(const String &l1, const String &l2, const String &l3, const String &l4, unsigned long ms) {
  infoLines[0] = l1;
  infoLines[1] = l2;
  infoLines[2] = l3;
  infoLines[3] = l4;
  infoHasAlt = false;
  infoUntil = millis() + ms;
  infoDrawn = false;
  infoUntilWeb = false;
}

// Second page for the message from showInfo()
void showInfoAlt(const String &l1, const String &l2, const String &l3, const String &l4) {
  infoAltLines[0] = l1;
  infoAltLines[1] = l2;
  infoAltLines[2] = l3;
  infoAltLines[3] = l4;
  infoHasAlt = true;
  infoDrawn = false;
}

// Keep the message from showInfo() up until a phone or laptop opens the web page, at most maxMs
void holdInfoUntilWeb(unsigned long maxMs) {
  infoUntil = millis() + maxMs;
  infoUntilWeb = true;
}

// How to reach the robot. Shown at every power-up and stays until a phone opens the page.
// home = the home WiFi is connected, ap = the robot's own network is on.
void showWiFiScreen(bool home, bool ap, unsigned long ms) {
  String apAddress = "http://" + WiFi.softAPIP().toString();
  if (home) {
    showInfo("WiFi connected!", "Open on your phone:", "http://" + WiFi.localIP().toString(), "(not https)", ms);
    if (ap) showInfoAlt("Or join WiFi " AP_SSID, "Password " AP_PASS, "then open:", apAddress);
  } else if (ap) {
    showInfo("Join WiFi " AP_SSID, "Password " AP_PASS, "then open:", apAddress, ms);
    if (strlen(HOME_WIFI_SSID)) showInfoAlt("Home WiFi:", HOME_WIFI_SSID, "connecting...", "");
  } else {
    showInfo("Connecting to WiFi", HOME_WIFI_SSID, "", "", ms);
  }
  if (!webUsed && mode != MODE_SLEEP) holdInfoUntilWeb(WIFI_INFO_MAX_MIN * 60000UL);
}

// Called by every web request from the page
void webPageSeen() {
  webUsed = true;
  lastWebAt = millis();
  if (!infoUntilWeb) return;
  infoUntilWeb = false;
  infoUntil = millis(); // closes on the next updateInfo()
  Serial.println("Web page connected");
}

// Returns true while a message is on screen
bool updateInfo() {
  static uint8_t drawnShift = 0;
  static bool drawnAlt = false;
  if (infoUntil == 0) return false;
  if ((long)(millis() - infoUntil) >= 0) {
    infoUntil = 0;
    infoUntilWeb = false;
    display.clearDisplay();
    if (mode == MODE_SLEEP) clockScreen.enter(); // back to the dimmed clock
    return false;
  }
  // A message that waits for the web page can stay for minutes: move it a little now and then
  uint8_t shift = (millis() / (PIXEL_SHIFT_SEC * 1000UL)) % 4;
  bool alt = infoHasAlt && (millis() / INFO_PAGE_MS) % 2;
  if (shift != drawnShift || alt != drawnAlt) infoDrawn = false;
  if (!infoDrawn) {
    if (mode == MODE_SLEEP) clockScreen.leave(); // full brightness, screen on even at night
    String *lines = alt ? infoAltLines : infoLines;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    for (int i = 0; i < 4; i++) {
      display.setCursor((shift & 1) * 2, 4 + (shift >> 1) * 2 + i * 16);
      display.print(lines[i]);
    }
    display.display();
    infoDrawn = true;
    drawnShift = shift;
    drawnAlt = alt;
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
  "  joy 30 80              joystick: turn -100..100, forward -100..100 (phone page)\n"
  "  x or stop              stop everything\n"
  "  speed 60               manual speed in percent (30-100)\n"
  "Show:\n"
  "  1-8 or dance, hello... play a behavior (list below)\n"
  "  mood happy             auto / normal / happy / angry / tired (auto follows time and weather)\n"
  "  sleep / wake / z       clock screen on / off / toggle\n"
  "  auto on / auto off / m random behaviors by itself\n"
  "Notes and timers:\n"
  "  msg Miss you <3        message on the OLED until seen (<3 = heart)\n"
  "  seen                   message seen, also stops the alarm\n"
  "  focus 25               focus timer in minutes, then a break (focus stop)\n"
  "  alarm 7:30             alarm every day (alarm off, alarm stop)\n"
  "  remind 60 Drink water  reminder every 60 min (remind list, remind off Drink water)\n"
  "My dance:\n"
  "  rec / rec stop         record driving, then save it as dance 9\n"
  "  9 or rec play          play it (rec delete removes it)\n"
  "Motors:\n"
  "  test                   each wheel forward and back at full power\n"
  "  minpwm 80              lowest motor power 0-255 (raise if wheels hum but don't turn)\n"
  "  maxpwm 255             highest motor power 0-255\n"
  "  kick 80                full-power start pulse in ms, 0 = off\n"
  "Info:\n"
  "  status                 mode, WiFi addresses, weather, timers\n"
  "  wifi                   list the 2.4 GHz networks the robot can see\n"
  "  ap on / ap off / ap auto  robot WiFi always on / off now / off when home WiFi works\n"
  "  settime 1761700000     set the clock (unix seconds), the phone page does this by itself\n"
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
  if (homePaused) s += "Home WiFi paused while a phone is on the robot WiFi\n";
  if (apOn) s += "Robot WiFi \"" AP_SSID "\": http://" + WiFi.softAPIP().toString() + (apKeepOn ? " (kept on)\n" : "\n");
  else s += "Robot WiFi: off to save power (ap on)\n";
  String city = getLocationCity();
  if (city.length()) s += "Location: " + city + " (found from WiFi)\n";
  else s += AUTO_LOCATION ? "Location: config.h until found from WiFi\n" : "Location: from config.h\n";
  struct tm t;
  if (ClockScreen::readTime(t)) {
    char buf[40];
    strftime(buf, sizeof(buf), "Clock: %a %b %d, %H:%M", &t);
    s += buf;
    if (!ClockScreen::isNight(t.tm_hour)) s += "\n";
    else s += NIGHT_SMALL_CLOCK ? " (night: small sleep clock)\n" : " (night: sleep screen off)\n";
  } else {
    s += "Clock: not set (no internet yet, opening the phone page sets it)\n";
  }
  Weather w = getWeather();
  if (w.valid) s += String("Weather: ") + String(w.temperature, 1) + (WEATHER_FAHRENHEIT ? "F " : "C ") + weatherText(w.code) + "\n";
  else s += "Weather: none yet\n";
  s += String("Mood: ") + moodName() + "\n";
  if (noteText.length()) s += String(noteKind) + " on screen: " + noteText + "\n";
  if (focusPhase != FOCUS_OFF) s += String(focusPhase == FOCUS_WORK ? "Focus: " : "Break: ") + focusLeftText() + "\n";
  s += alarmMinute < 0 ? String("Alarm: off\n") : "Alarm: every day at " + clockText(alarmMinute) + (alarmRinging ? " (ringing)\n" : "\n");
  s += reminderList() + "\n";
  s += myDance.count ? "My dance: " + String(danceSeconds(), 1) + " s\n" : String("My dance: none\n");
  s += "Slowest loop: " + String(loopMaxMs) + " ms (above about 50 ms driving feels laggy)";
  return s;
}

const char *moodName() {
  if (moodAuto) return "auto";
  switch (restMood) {
    case HAPPY: return "happy";
    case ANGRY: return "angry";
    case TIRED: return "tired";
    default: return "normal";
  }
}

// Text as a JSON string
String jsonQuote(const String &text) {
  String s = "\"";
  for (unsigned int i = 0; i < text.length(); i++) {
    char c = text[i];
    if (c == '"' || c == '\\') {
      s += '\\';
      s += c;
    } else if ((uint8_t)c < 32) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", c); // the heart is character 3
      s += buf;
    } else {
      s += c;
    }
  }
  return s + "\"";
}

String statusJson() {
  struct tm t;
  char alarm[6] = "";
  if (alarmMinute >= 0) snprintf(alarm, sizeof(alarm), "%02d:%02d", alarmMinute / 60, alarmMinute % 60);
  String reminderJson;
  for (uint8_t i = 0; i < REMINDER_COUNT; i++) {
    if (!reminders[i].everyMin) continue;
    reminderJson += String(reminderJson.length() ? "," : "") + "{\"t\":" + jsonQuote(reminders[i].text)
                  + ",\"m\":" + reminders[i].everyMin + "}";
  }
  long focusSecs = focusPhase == FOCUS_OFF ? 0 : max(0L, (long)(focusEndAt - millis()) / 1000);
  unsigned long recSecs = recording ? (millis() - recStartedAt) / 1000 : (unsigned long)danceSeconds();

  String s = String("{\"mode\":\"") + modeName() + "\",\"auto\":" + (autoMode ? "true" : "false")
           + ",\"speed\":" + manualSpeed + ",\"ip\":\"" + homeAddress() + "\""
           + ",\"clock\":" + (ClockScreen::readTime(t) ? "true" : "false")
           + ",\"mood\":\"" + moodName() + "\""
           + ",\"note\":" + jsonQuote(noteText) + ",\"noteKind\":\"" + noteKind + "\""
           + ",\"focus\":" + focusSecs + ",\"focusPhase\":\"" + (focusPhase == FOCUS_WORK ? "work" : focusPhase == FOCUS_BREAK ? "break" : "") + "\""
           + ",\"alarm\":\"" + alarm + "\",\"ringing\":" + (alarmRinging ? "true" : "false")
           + ",\"reminders\":[" + reminderJson + "]"
           + ",\"rec\":" + (recording ? "true" : "false") + ",\"recSec\":" + recSecs
           + ",\"recMax\":" + RECORD_MAX_SEC + ",\"dance\":" + (myDance.count ? "true" : "false")
           + ",\"ms\":" + loopMaxMs + "}";
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
  String raw = line; // messages and reminders keep their capital letters
  line.toLowerCase();
  if (line.length() == 0) return "";
  lastUserAction = millis();

  int space = line.indexOf(' ');
  String cmd = space < 0 ? line : line.substring(0, space);
  String arg = space < 0 ? "" : line.substring(space + 1);
  String rawArg = space < 0 ? "" : raw.substring(space + 1);
  arg.trim();
  rawArg.trim();

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

  if (cmd == "joy") {
    int gap = arg.indexOf(' ');
    if (gap < 0) return "Use: joy <x> <y>, both -100..100";
    joystickDrive(arg.toInt(), arg.substring(gap + 1).toInt(), MANUAL_TIMEOUT_MS);
    return "";
  }
  if (cmd == "x" || cmd == "stop") {
    stopAlarm();
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
    moodAuto = false;
    if (arg == "auto") moodAuto = true;
    else if (arg == "normal") restMood = DEFAULT;
    else if (arg == "happy") restMood = HAPPY;
    else if (arg == "angry") restMood = ANGRY;
    else if (arg == "tired") restMood = TIRED;
    else {
      moodAuto = true;
      return "Moods: auto, normal, happy, angry, tired";
    }
    if (mode == MODE_REST || mode == MODE_MANUAL) applyRestFace();
    return moodAuto ? "Mood auto: follows the time of day and the weather" : "Mood " + arg;
  }
  if (cmd == "msg" || cmd == "message") {
    if (!rawArg.length()) return noteText.length() ? "On screen: " + noteText : "Use: msg Miss you <3";
    String text = cleanNoteText(rawArg);
    if (!text.length()) return "The OLED only shows plain letters, numbers and hearts";
    showNote("Message", text, 0);
    return "Message sent";
  }
  if (cmd == "seen") {
    stopAlarm();
    if (!noteText.length()) return "Nothing on screen";
    clearNote();
    return "Seen";
  }
  if (cmd == "focus") {
    if (arg == "stop" || arg == "off") {
      focusPhase = FOCUS_OFF;
      return "Focus timer stopped";
    }
    if (!arg.length()) {
      if (focusPhase == FOCUS_OFF) return "Use: focus 25 (minutes)";
      return String(focusPhase == FOCUS_WORK ? "Focus: " : "Break: ") + focusLeftText();
    }
    int minutes = arg.toInt();
    if (minutes < 1 || minutes > 180) return "Use: focus 1-180 (minutes)";
    startFocus(minutes);
    return "Focus " + String(minutes) + " min, then a " + FOCUS_BREAK_MIN + " min break";
  }
  if (cmd == "alarm") {
    if (arg == "stop") {
      stopAlarm();
      return "Alarm stopped";
    }
    if (arg == "off") {
      stopAlarm();
      alarmMinute = -1;
      saveTimers();
      return "Alarm off";
    }
    if (!arg.length()) return alarmMinute < 0 ? String("Alarm off. Use: alarm 7:30") : "Alarm every day at " + clockText(alarmMinute);
    int minute = parseClockTime(arg);
    if (minute < 0) return "Use: alarm 7:30, alarm 19:45 or alarm 7:30pm";
    alarmMinute = minute;
    saveTimers();
    struct tm t;
    return "Alarm every day at " + clockText(minute)
         + (ClockScreen::readTime(t) ? "" : " (the clock isn't set yet: open the phone page)");
  }
  if (cmd == "remind") {
    if (!arg.length() || arg == "list") return reminderList();
    if (arg == "off" || arg.startsWith("off ")) return removeReminder(rawArg.substring(3));
    int gap = rawArg.indexOf(' ');
    int minutes = arg.toInt();
    if (minutes < 1 || minutes > 720 || gap < 0) return "Use: remind 60 Drink water (every 1-720 minutes)";
    return setReminder(minutes, rawArg.substring(gap + 1));
  }
  if (cmd == "rec" || cmd == "record") {
    if (arg == "" || arg == "start") return startRecording();
    if (arg == "stop" || arg == "save") return stopRecording();
    if (arg == "play") return playMyDance();
    if (arg == "delete") {
      if (recording) stopRecording();
      myDance.count = 0;
      prefs.remove("dance");
      return "My dance deleted";
    }
    return "Use: rec, rec stop, rec play, rec delete";
  }
  if (line == "9" || line == "my dance" || line == "mydance") return playMyDance();
  if (cmd == "ap") {
    if (arg == "on") {
      apKeepOn = true;
      setAp(true);
      return "Robot WiFi on, and it stays on (ap auto: off again when the home WiFi works)";
    }
    if (arg == "off") {
      if (WiFi.status() != WL_CONNECTED) return "Not without home WiFi, the robot couldn't be reached";
      apKeepOn = false;
      setAp(false);
      return "Robot WiFi off";
    }
    if (arg == "auto") {
      apKeepOn = false;
      return "Robot WiFi switches off by itself while the home WiFi works";
    }
    return String("Robot WiFi is ") + (apOn ? "on" : "off") + ". Use: ap on, ap off, ap auto";
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
  if (cmd == "settime") {
    long secs = arg.toInt();
    if (secs < 1700000000) return "Use: settime <unix time in seconds>";
    struct timeval tv = { secs, 0 };
    settimeofday(&tv, nullptr); // the time zone from config.h (or found from WiFi) still applies
    return "Clock set";
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

// Phone remote, made for holding the phone upright. Three tabs at the bottom, like an app:
//   Drive - big joystick, speed, stop, and recording a dance
//   Play  - behaviors, mood, sleep / wake, auto mode
//   Notes - message on the robot's screen, focus timer, alarm, reminders
// Add it to the phone's home screen ("Share > Add to Home Screen" on iPhone,
// "menu > Add to Home screen" on Android) and it opens fullscreen like an app.
const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="theme-color" content="#111214">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="RoboFace">
<link rel="manifest" href="/manifest.json"><link rel="icon" href="/icon.svg">
<title>RoboFace</title><style>
:root{--bg:#111214;--card:#1c1d21;--btn:#2a2b31;--text:#ececf1;--dim:#8d8f99;--accent:#0a84ff;--stop:#d23b3b;--ok:#34c759}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;height:100%;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,sans-serif;overscroll-behavior:none}
body{user-select:none;-webkit-user-select:none;-webkit-touch-callout:none}
.app{display:flex;flex-direction:column;height:100vh;height:100dvh;max-width:460px;margin:0 auto}
header{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:calc(8px + env(safe-area-inset-top)) calc(14px + env(safe-area-inset-right)) 6px calc(14px + env(safe-area-inset-left))}
h1{font-size:19px;margin:0}
#st{display:flex;align-items:center;gap:6px;color:var(--dim);font-size:12px;min-width:0}
#stt{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#dot{flex:none;width:9px;height:9px;border-radius:50%;background:var(--stop)}
#dot.on{background:var(--ok)}
main{flex:1;min-height:0;overflow-y:auto;padding:4px calc(14px + env(safe-area-inset-right)) 10px calc(14px + env(safe-area-inset-left))}
section{display:flex;flex-direction:column;gap:8px;min-height:100%}
section[hidden]{display:none}
.tabs{flex:none;display:grid;grid-template-columns:repeat(3,1fr);background:var(--card);border-top:1px solid #26272c;
  padding:4px calc(8px + env(safe-area-inset-right)) calc(4px + env(safe-area-inset-bottom)) calc(8px + env(safe-area-inset-left))}
.tabs button{background:none;color:var(--dim);padding:6px 0 4px;font-size:12px;display:flex;flex-direction:column;align-items:center;gap:2px;position:relative}
.tabs button b{font-size:21px;font-weight:400;line-height:1.1}
.tabs button.on{color:var(--accent);background:none}
.tabs i{position:absolute;top:4px;left:calc(50% + 12px);width:8px;height:8px;border-radius:50%;background:var(--stop);display:none}
.tabs i.on{display:block}
h2{font-size:12px;font-weight:600;color:var(--dim);text-transform:uppercase;letter-spacing:.05em;margin:8px 0 -2px}
.grid2{display:grid;grid-template-columns:repeat(2,1fr);gap:6px}
.grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}
.grid4{display:grid;grid-template-columns:repeat(4,1fr);gap:6px}
.grid5{display:grid;grid-template-columns:repeat(5,1fr);gap:5px}
.grid5 button{font-size:13px}
.big button{padding:16px 0;font-size:15px}
.row{display:flex;gap:6px}
.row input{flex:1;min-width:0}
input[type=text],input[type=time]{font:inherit;font-size:16px;padding:9px 10px;border-radius:11px;border:1px solid #33353d;background:#18191d;color:var(--text);user-select:text;-webkit-user-select:text}
input[type=time]{color-scheme:dark}
.info{color:var(--dim);font-size:13px;line-height:1.4;min-height:18px}
button{font:inherit;font-size:14px;padding:11px 0;border:0;border-radius:11px;background:var(--btn);color:var(--text);touch-action:manipulation;cursor:pointer;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.row button{padding:11px 14px}
button:active,button.on{background:var(--accent)}
button:disabled{opacity:.45}
button.acc{background:var(--accent)}
button.red,button.red.on{background:var(--stop)}
#t-drive{align-items:center;justify-content:center;gap:14px}
#pad{position:relative;flex:none;width:min(84vw,50dvh,360px);aspect-ratio:1;border-radius:50%;touch-action:none;margin-top:6px;
  background:radial-gradient(circle,#24262c 0 30%,#1c1d21 31% 100%);border:2px solid #2f3138}
#pad::before,#pad::after{content:"";position:absolute;background:#2f3138}
#pad::before{left:50%;top:10%;bottom:10%;width:2px;margin-left:-1px}
#pad::after{top:50%;left:10%;right:10%;height:2px;margin-top:-1px}
#pad.rec{border-color:var(--stop);box-shadow:0 0 0 4px #d23b3b33}
#knob{position:absolute;left:50%;top:50%;width:34%;aspect-ratio:1;margin:-17% 0 0 -17%;border-radius:50%;z-index:1;
  background:#3a3c44;box-shadow:0 4px 14px #0008;transition:transform .12s ease-out}
#pad.on #knob{background:var(--accent);transition:none}
.speed{display:flex;align-items:center;gap:10px;width:100%}
.speed input{flex:1;min-width:0;accent-color:var(--accent)}
#spv{width:40px;color:var(--dim);font-size:14px}
.stop{background:var(--stop);font-weight:600;font-size:16px}
#stop{width:100%;padding:14px 0}
#t-drive .grid2{width:100%}
#toast{position:fixed;left:50%;top:calc(48px + env(safe-area-inset-top));transform:translateX(-50%);max-width:90vw;z-index:9;
  background:#2e3036;color:var(--text);font-size:14px;padding:9px 14px;border-radius:12px;box-shadow:0 6px 20px #000a;
  opacity:0;pointer-events:none;transition:opacity .25s}
#toast.on{opacity:1}
.keys{display:none;color:#5d5f68;font-size:12px;text-align:center;line-height:1.5}
@media (hover:hover) and (pointer:fine){.keys{display:block}}
@media (orientation:landscape) and (max-height:520px){
  #t-drive{display:grid;grid-template-columns:auto 1fr;align-content:center;column-gap:18px}
  #t-drive[hidden]{display:none}
  #pad{grid-row:1/5;width:min(70dvh,280px)}
}
</style></head><body><div class="app">
<header><h1>RoboFace</h1><div id="st"><span id="dot"></span><span id="stt">connecting...</span></div></header>
<main>
<section id="t-drive">
<div id="pad"><div id="knob"></div></div>
<div class="speed"><span>Speed</span><input type="range" id="spd" min="30" max="100" step="5"><span id="spv"></span></div>
<button id="stop" class="stop">Stop</button>
<div class="grid2"><button id="recBtn">&#9210; Record dance</button><button data-c="9" id="playBtn">&#9654; My dance</button></div>
<div class="info" id="recSt"></div>
<div class="keys">Keyboard: W A S D or arrows (hold, combine for curves), Space stop, 1-9 behaviors, Z sleep/wake, M auto mode</div>
</section>
<section id="t-play" hidden>
<h2>Behaviors</h2><div class="grid3 big">)HTML";

const char PAGE_TAIL[] PROGMEM = R"HTML(<button data-c="9" id="play9">My dance</button></div>
<h2>Mood</h2><div class="grid5">
<button data-c="mood auto" data-mood="auto">Auto</button><button data-c="mood normal" data-mood="normal">Normal</button><button data-c="mood happy" data-mood="happy">Happy</button><button data-c="mood angry" data-mood="angry">Angry</button><button data-c="mood tired" data-mood="tired">Tired</button>
</div>
<div class="info">Auto: happy in the morning, sleepy at night, sweats when it&#39;s hot outside.</div>
<h2>Robot</h2>
<div class="grid2">
<button data-c="sleep">&#127769; Sleep</button><button data-c="wake">&#9728;&#65039; Wake up</button><button id="auto">&#129302; Wander by itself</button><button data-c="x" class="stop">Stop</button>
</div>
<button data-c="rec delete" id="delDance">Delete my dance</button>
</section>
<section id="t-notes" hidden>
<h2>Message on the robot</h2>
<div class="row"><input type="text" id="msg" maxlength="80" placeholder="Write on the robot&#39;s screen" enterkeyhint="send" autocomplete="off"><button id="send" class="acc">Send</button></div>
<div class="grid3"><button data-m="Miss you <3">Miss you &#10084;&#65039;</button><button data-m="Good night! <3">Good night</button><button data-m="I&#39;m home!">I&#39;m home</button></div>
<div class="info" id="noteSt"></div>
<button id="seen">&#10003; Mark as seen</button>
<h2>Focus timer</h2>
<div class="grid4"><button data-f="15">15 min</button><button data-f="25">25 min</button><button data-f="45">45 min</button><button data-c="focus stop">Stop</button></div>
<div class="info" id="focusSt"></div>
<h2>Alarm, every day</h2>
<div class="row"><input type="time" id="alarmT"><button id="alarmSet" class="acc">Set</button><button data-c="alarm off">Off</button></div>
<div class="info" id="alarmSt"></div>
<h2>Reminders</h2>
<div class="grid2"><button data-r="60|Drink water">&#128167; Water &middot; 60 min</button><button data-r="45|Stand up and stretch">&#129496; Stretch &middot; 45 min</button></div>
<div class="info" id="remSt"></div>
</section>
</main>
<nav class="tabs">
<button data-tab="drive" class="on"><b>&#127918;</b>Drive<i id="db"></i></button>
<button data-tab="play"><b>&#127917;</b>Play</button>
<button data-tab="notes"><b>&#128172;</b>Notes<i id="nb"></i></button>
</nav>
</div>
<div id="toast"></div>
<script>
const $=id=>document.getElementById(id);
let last={};
function online(on){$('dot').classList.toggle('on',on);if(!on)$('stt').textContent='offline';}
function cmd(c){return fetch('/cmd?c='+encodeURIComponent(c)).then(r=>r.text()).catch(()=>{online(false);return '';});}
function buzz(ms){if(navigator.vibrate)navigator.vibrate(ms);}
let toastTimer=null;
function toast(t){if(!t||t==='ok')return;$('toast').textContent=t;$('toast').classList.add('on');clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').classList.remove('on'),2600);}
const hearts=t=>t.replace(/\x03/g,'❤️');
function run(c){return cmd(c).then(r=>{toast(r);poll();});}

// Tabs
function showTab(name){
  releaseAll();
  document.querySelectorAll('.tabs button').forEach(b=>b.classList.toggle('on',b.dataset.tab===name));
  document.querySelectorAll('main section').forEach(s=>s.hidden=s.id!=='t-'+name);
  try{localStorage.setItem('tab',name);}catch(e){}
}
document.querySelectorAll('.tabs button').forEach(b=>b.addEventListener('click',()=>showTab(b.dataset.tab)));

// Buttons: data-c = command, data-m = message, data-f = focus minutes, data-r = "minutes|reminder text"
document.addEventListener('click',e=>{
  const b=e.target.closest('button');if(!b||b.disabled)return;
  if(b.dataset.c)run(b.dataset.c);
  else if(b.dataset.m)sendMsg(b.dataset.m);
  else if(b.dataset.f)run('focus '+b.dataset.f);
  else if(b.dataset.r){const [m,t]=b.dataset.r.split('|');run(b.classList.contains('on')?'remind off '+t:'remind '+m+' '+t);}
});

// Messages
function sendMsg(t){t=t.trim();if(!t)return;cmd('msg '+t).then(r=>{toast(r);if(r==='Message sent')$('msg').value='';poll();});}
$('send').addEventListener('click',()=>sendMsg($('msg').value));
$('msg').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();sendMsg($('msg').value);}});
$('seen').addEventListener('click',()=>run('seen'));

// Timers
$('alarmSet').addEventListener('click',()=>{const v=$('alarmT').value;if(v)run('alarm '+v);else toast('Pick a time first');});

// Dance recording
$('recBtn').addEventListener('click',()=>run(last.rec?'rec stop':'rec'));

// Joystick. x/y are -100..100, sent as "joy x y" about 12 times a second while held.
// The robot stops by itself if no command arrives for 0.7 s, so a dropped connection is safe.
const pad=$('pad'),knob=$('knob');
let jx=0,jy=0,touchId=null,busy=false,stopAfter=false,sent='',sentAt=0;
const keys=new Set();
function driving(){return touchId!==null||keys.size>0;}
function moveKnob(e){
  const r=pad.getBoundingClientRect(),R=r.width/2,max=R*0.66;
  let dx=e.clientX-r.left-R,dy=e.clientY-r.top-R;
  const d=Math.hypot(dx,dy);
  if(d>max){dx*=max/d;dy*=max/d;}
  knob.style.transform='translate('+dx+'px,'+dy+'px)';
  jx=Math.round(dx/max*100);jy=Math.round(-dy/max*100);
  if(Math.hypot(jx,jy)<15){jx=0;jy=0;} // dead zone in the middle
}
function stopNow(){
  sent='';
  if(busy)stopAfter=true; // also stop again once the request on its way has arrived
  cmd('x');
}
pad.addEventListener('pointerdown',e=>{
  e.preventDefault();
  touchId=e.pointerId;pad.setPointerCapture(touchId);pad.classList.add('on');
  moveKnob(e);buzz(8);
});
pad.addEventListener('pointermove',e=>{if(e.pointerId===touchId)moveKnob(e);});
['pointerup','pointercancel'].forEach(ev=>pad.addEventListener(ev,e=>{
  if(e.pointerId!==touchId)return;
  touchId=null;pad.classList.remove('on');knob.style.transform='';jx=jy=0;
  if(!keys.size)stopNow();
}));
pad.addEventListener('touchmove',e=>e.preventDefault(),{passive:false}); // never scroll the page while steering
setInterval(()=>{
  if(!driving()||busy)return;
  const c='joy '+jx+' '+jy,now=Date.now();
  if(c===sent&&now-sentAt<250)return; // only resend unchanged values as a keep-alive
  sent=c;sentAt=now;busy=true;
  cmd(c).finally(()=>{busy=false;if(stopAfter){stopAfter=false;if(!driving())cmd('x');}});
},80);

// Keyboard on a laptop: keys combine, so W + D drives a curve to the right. Not while typing.
const keyMap={w:'w',arrowup:'w',s:'s',arrowdown:'s',a:'a',arrowleft:'a',d:'d',arrowright:'d'};
const typing=e=>e.target.tagName==='INPUT';
function keysToJoy(){
  jx=(keys.has('d')?100:0)-(keys.has('a')?100:0);
  jy=(keys.has('w')?100:0)-(keys.has('s')?100:0);
}
addEventListener('keydown',e=>{
  if(typing(e))return;
  const k=e.key.toLowerCase();
  if(keyMap[k]){e.preventDefault();keys.add(keyMap[k]);keysToJoy();}
  else if(k===' '||k==='x'){e.preventDefault();keys.clear();stopNow();}
  else if(/^[1-9]$/.test(k)||k==='z'||k==='m')cmd(k);
});
addEventListener('keyup',e=>{
  const k=keyMap[e.key.toLowerCase()];
  if(!k||!keys.has(k))return;
  keys.delete(k);keysToJoy();
  if(!driving())stopNow();
});
function releaseAll(){if(driving()){keys.clear();touchId=null;pad.classList.remove('on');knob.style.transform='';stopNow();}}
addEventListener('blur',releaseAll);
document.addEventListener('visibilitychange',()=>{if(document.hidden)releaseAll();});

$('stop').addEventListener('click',()=>{buzz(30);keys.clear();stopNow();});
$('spd').addEventListener('input',()=>{$('spv').textContent=$('spd').value+'%';});
$('spd').addEventListener('change',()=>cmd('speed '+$('spd').value));
$('auto').addEventListener('click',()=>{
  const on=!$('auto').classList.contains('on');
  $('auto').classList.toggle('on',on);
  cmd('auto '+(on?'on':'off'));
});

function mins(s){return s>=60?Math.ceil(s/60)+' min':s+' s';}
function show(s){
  last=s;
  $('stt').textContent=(s.ringing?'⏰ alarm! tap Stop · ':'')+s.mode+(s.ip?' · '+s.ip:'')+' · '+s.ping+' ms';
  $('auto').classList.toggle('on',s.auto);
  if(document.activeElement!==$('spd'))$('spd').value=s.speed;
  $('spv').textContent=$('spd').value+'%';
  document.querySelectorAll('[data-mood]').forEach(b=>b.classList.toggle('on',b.dataset.mood===s.mood));
  // Drive tab: dance recording
  $('db').classList.toggle('on',s.rec);
  pad.classList.toggle('rec',s.rec);
  $('recBtn').innerHTML=s.rec?'&#9209; Stop &amp; save':'&#9210; Record dance';
  $('recBtn').classList.toggle('red',s.rec);
  $('playBtn').disabled=$('play9').disabled=!s.dance||s.rec;
  $('delDance').disabled=!s.dance;
  $('recSt').textContent=s.rec?'Recording… '+s.recSec+' of '+s.recMax+' s. Drive, then Stop & save.'
    :s.dance?'My dance: '+s.recSec+' s. Record again to replace it.':'Record a dance: press Record, drive, then Stop & save.';
  // Notes tab
  $('nb').classList.toggle('on',!!s.note||!!s.focusPhase||s.ringing);
  $('noteSt').textContent=s.note?'On the robot ('+s.noteKind.toLowerCase()+'): “'+hearts(s.note)+'”':'Nothing on the robot’s screen. Type <3 for a heart.';
  $('seen').disabled=!s.note;
  $('focusSt').textContent=s.focusPhase==='work'?'Focusing: '+mins(s.focus)+' left. The robot stays quiet.'
    :s.focusPhase==='break'?'Break: '+mins(s.focus)+' left':'The robot keeps quiet while you focus, then dances for your break.';
  if(document.activeElement!==$('alarmT')&&s.alarm&&!$('alarmT').value)$('alarmT').value=s.alarm;
  $('alarmSt').textContent=s.ringing?'Ringing! Stop it with Stop or Mark as seen.':s.alarm?'Set for '+s.alarm+' every day.':'Off. The robot dances and shows the time.';
  document.querySelectorAll('[data-r]').forEach(b=>b.classList.toggle('on',s.reminders.some(r=>r.t===b.dataset.r.split('|')[1])));
  $('remSt').textContent=s.reminders.length?s.reminders.map(r=>r.t+' every '+r.m+' min').join(' · '):'Tap to turn on. The robot shows it and wiggles.';
}
function poll(){
  const t0=performance.now();
  fetch('/status').then(r=>r.json()).then(s=>{
    online(true);
    s.ping=Math.round(performance.now()-t0);
    show(s);
    if(!s.clock)sendTime(); // e.g. the robot restarted while this page stayed open
  }).catch(()=>online(false));
}
// The robot has no clock without internet, so it takes the time from the phone
function sendTime(){cmd('settime '+Math.floor(Date.now()/1000));}
try{const t=localStorage.getItem('tab');if(t&&$('t-'+t))showTab(t);}catch(e){}
sendTime();
poll();setInterval(poll,2000);
</script></body></html>)HTML";

const char MANIFEST[] PROGMEM = R"JSON({"name":"RoboFace","short_name":"RoboFace","start_url":"/","display":"standalone",
"background_color":"#111214","theme_color":"#111214","icons":[{"src":"/icon.svg","sizes":"any","type":"image/svg+xml"}]})JSON";

const char ICON_SVG[] PROGMEM = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">
<rect width="100" height="100" rx="22" fill="#111214"/>
<rect x="18" y="32" width="26" height="36" rx="8" fill="#0a84ff"/><rect x="56" y="32" width="26" height="36" rx="8" fill="#0a84ff"/>
</svg>)SVG";

void handleRoot() {
  webPageSeen();
  String html = PAGE_HEAD;
  for (uint8_t i = 0; i < behaviorCount; i++) {
    html += "<button onclick=\"cmd('" + String(i + 1) + "')\">" + behaviors[i].name + "</button>";
  }
  html += PAGE_TAIL;
  server.send(200, "text/html", html);
}

void handleCmd() {
  webPageSeen();
  String reply = runCommand(server.arg("c"));
  server.send(200, "text/plain", reply.length() ? reply : "ok");
}

void handleStatus() {
  webPageSeen(); // a page left open on the phone polls this, so it counts too
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

  apOn = ap;
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
    Serial.println("No home WiFi set in config.h: weather is off, the clock is set by the phone page");
    setenv("TZ", TIME_ZONE, 1);
    tzset();
  }

  // At every power-up, show how to connect instead of the face, until a phone opens the page
  showWiFiScreen(false, ap, 0);

  // http://roboface.local on the home network
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);

  server.on("/", handleRoot);
  server.on("/cmd", handleCmd);
  server.on("/status", handleStatus);
  server.on("/manifest.json", []() { server.send_P(200, "application/manifest+json", MANIFEST); });
  server.on("/icon.svg", []() { server.send_P(200, "image/svg+xml", ICON_SVG); });
  server.begin();
}

// When the phone leaves the robot's WiFi, or its page goes quiet for WEB_LOST_SEC, show how to
// connect again until a phone opens the page. Not while asleep, the dimmed clock stays.
void checkPhone() {
  static unsigned long lastCheck = 0;
  static uint8_t lastStations = 0;
  unsigned long now = millis();
  if (now - lastCheck < 500) return;
  lastCheck = now;

  uint8_t stations = (WiFi.getMode() & WIFI_AP) ? WiFi.softAPgetStationNum() : 0;
  bool leftAp = stations == 0 && lastStations > 0;
  lastStations = stations;
  bool quiet = now - lastWebAt > WEB_LOST_SEC * 1000UL;
  if (!webUsed || !(leftAp || quiet)) return;

  webUsed = false;
  Serial.println(leftAp ? "Phone left the robot WiFi" : "Web page closed");
  if (mode != MODE_SLEEP) showWiFiScreen(WiFi.status() == WL_CONNECTED, WiFi.getMode() & WIFI_AP, 0);
}

// When the home WiFi connects, show the address on serial and on the OLED. Until a phone has
// opened the page it stays on screen, after that only for a few seconds.
// Reports why a connection fails, and switches between the home and backup network if needed.
void checkWiFi() {
  static bool wasConnected = false;
  static uint8_t reportedReason = 0;
  bool connected = WiFi.status() == WL_CONNECTED;

  if (connected && !wasConnected) {
    String ip = WiFi.localIP().toString();
    Serial.println("WiFi \"" + WiFi.SSID() + "\" connected: http://" + ip + "  or  http://" HOSTNAME ".local");
    showWiFiScreen(true, WiFi.getMode() & WIFI_AP, 6000);
    wifiFailReason = 0;
  }
  wasConnected = connected;
  if (connected || strlen(HOME_WIFI_SSID) == 0 || homePaused) return;

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
  setCpuFrequencyMhz(CPU_MHZ);
  Serial.begin(115200);

  // Saved alarm, reminders and dance
  prefs.begin("robot");
  alarmMinute = prefs.getInt("alarm", -1);
  if (prefs.isKey("remind")) prefs.getBytes("remind", reminders, sizeof(reminders));
  for (uint8_t i = 0; i < REMINDER_COUNT; i++) {
    reminders[i].text[sizeof(reminders[i].text) - 1] = 0;
    if (reminders[i].everyMin > 720) reminders[i].everyMin = 0;
    reminderDueAt[i] = millis() + reminders[i].everyMin * 60000UL;
  }
  clockScreen.alarmSet = alarmMinute >= 0;
  loadDance();

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
    for (;;);
  }

  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 50); // screen size, max fps
  roboEyes.setAutoblinker(ON, 3, 2); // blink every 3-5 s

  setupWiFi();
  setupOTA();
  Serial.println("Type help for commands");
  lastUserAction = millis();
  startBehavior(wakeUpBehavior);
}

void updateScreen() {
  if (updateInfo()) return;  // WiFi address or another message on screen
  if (mode == MODE_SLEEP) clockScreen.update();
  else if (!updateCard()) roboEyes.update(); // never use long delay()s, the eyes and motor ramps need this to run often
}

void loop() {
  unsigned long start = millis();
#if OTA_ENABLED
  ArduinoOTA.handle();
#endif
  server.handleClient();
  handleSerial();
  checkWiFi();
  checkPhone();
  updatePower();
  applyAutoTimeZone();
  updateTimers();
  updateMode();
  drive.update();
  updateScreen();
  trackLoopTime(start);
  if (mode == MODE_SLEEP) delay(10); // nothing moves while asleep: let the CPU rest to save power
}

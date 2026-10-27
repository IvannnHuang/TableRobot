# RoboFace: Companion Robot Project Plan

Two small two-wheeled robots, one for me and one for my partner. Each robot has an animated face, drives on two wheels, listens and talks, and can interact with the other robot over the internet.

- **v1, the current build:** XIAO ESP32-C3 + 0.96" OLED + motor driver, controlled from a phone or laptop.
- **v2, the target build:** Waveshare ESP32-S3 1.43" AMOLED touch board + motors + microphone + speaker, linked to the partner's robot and to a cloud AI.

Last updated: 2026-10-27

---

## Current progress (2026-10-27)
- **Firmware:** the v1 ESP32-C3 code is complete and compiles (motors, 0.96" OLED face, sleep clock and weather, web/WiFi/serial/USB control). See [section 2.3](#23-firmware-folder-firmwarerobot_face).
- **CAD:** first version of the v1 robot frame (top shell, bottom plate, wheel connector), sized for the ESP32-C3, the two motors and the 0.96" OLED. See [section 2.8](#28-mechanical-cad-v1-frame).
- **Hardware:** waiting on replacement motors, a DRV8833, a 1S LiPo and a new XIAO ESP32-C3 (the old one was damaged during the bench test).
- **Repo:** everything lives in this git repo, `TableRobot`.

### Repo layout
```
TableRobot/
├── CLAUDE.md                       context for Claude Code sessions in this repo
├── README.md                       quick start
├── docs/ROBOT_PLAN.md              this plan
├── firmware/robot_face/            Arduino sketch for the v1 ESP32-C3 robot
│   ├── robot_face.ino
│   ├── config.h                    settings (pins, motors, sleep, location)
│   ├── secrets.example.h           template → copy to secrets.h (git-ignored) for WiFi passwords
│   ├── motors.h, weather.h, clock_screen.h, FluxGarage_RoboEyes.h
├── tools/laptop_control/serial_control.html   USB control page (Chrome/Edge)
└── cad/                            v1 frame: STL parts + sliced print file
```

---

## Contents
1. [Goals](#1-goals)
2. [v1: XIAO ESP32-C3 robot (current)](#2-v1-xiao-esp32-c3-robot-current)
3. [v2: ESP32-S3 AMOLED touch robot (target)](#3-v2-esp32-s3-amoled-touch-robot-target)
4. [System architecture](#4-system-architecture)
5. [Upgrade roadmap](#5-upgrade-roadmap)
6. [Safety rules and lessons learned](#6-safety-rules-and-lessons-learned)
7. [Budget](#7-budget)
8. [Open questions](#8-open-questions)

---

## 1. Goals
- A face that clearly shows emotions: happy, angry, tired, curious, scared, sleepy.
- Body language through the wheels: dance, nod, shake "no", turn toward you.
- Control from a phone or laptop: on the same WiFi, from anywhere, or over USB.
- The two robots interact over the internet: hugs, moods, voice messages, dancing together.
- Listening and talking: wake word, voice commands, and AI conversation through the cloud.
- Reacting to touch, being picked up, shaken or bumped.
- Fun app features: funny faces, sounds and music on the partner's robot.

---

## 2. v1: XIAO ESP32-C3 robot (current)

### 2.1 Hardware
| Part | Model | Notes |
|---|---|---|
| Microcontroller | Seeed XIAO ESP32-C3 | 2.4 GHz WiFi only, 4 MB flash, 11 GPIO |
| Display | ELEGOO 0.96" SSD1306 128×64 I2C, address 0x3C | Two-colour: top 16 rows yellow, bottom 48 rows blue (fixed by the panel). Monochrome pixels |
| Motor driver | ~~L298N~~ → **DRV8833** | The L298N loses 2–3 V, so the motors only hummed. The DRV8833 works from a 3.7 V battery and has overcurrent protection |
| Motors | 2× N20 (1218GE) gear motor, 16 RPM listing (3 V/6 V versions mixed) | ⚠️ The tested motor is **mechanically jammed**: 20 Ω coil, draws the full stall current (≈0.3 A at 6 V), doesn't turn. Replace with **N20 6 V, 50–100 RPM** |
| Battery | ~~ER14250~~ → **1S LiPo 3.7 V, 402540 (≈350 mAh), 2C+, with protection** | Space available: 26 × 50 × 5 mm. ER14250 is **not rechargeable** and too weak, so don't use it |

### 2.2 Wiring (v1 with DRV8833)
| XIAO pin | GPIO | Connects to |
|---|---|---|
| D0 / A0 | 2 | DRV8833 IN1 (left motor), through 1 kΩ |
| D1 / A1 | 3 | DRV8833 IN2 (left motor), through 1 kΩ |
| D2 / A2 | 4 | DRV8833 IN3 (right motor), through 1 kΩ |
| D3 / A3 | 5 | DRV8833 IN4 (right motor), through 1 kΩ |
| D4 | 6 | OLED SDA |
| D5 | 7 | OLED SCL |
| 3V3 | — | OLED VCC, DRV8833 EEP (enable) |
| GND | — | OLED GND, DRV8833 GND, battery − (**shared ground required**) |
| D6–D10 | 21, 20, 8, 9, 10 | Free (planned for I2S audio, see 2.6) |

DRV8833 pins:
- **VCC** = motor power. **GND** = shared ground.
- **OUT1/OUT2** = left motor, **OUT3/OUT4** = right motor.
- **EEP** = enable, connected to 3V3. **ULT** = fault output, not connected.

**Power on battery:**
```
LiPo (through UMLIFE charger OUT+/OUT−)
  OUT+ ── switch ── Schottky diode (SS14) ──┬── XIAO 5V pin
                                           └── DRV8833 VCC  (+ 220–470 µF capacitor)
  OUT− ────────────────────────────────────┴── XIAO GND ── DRV8833 GND
```
- The diode stops USB from back-feeding the battery when the cable is plugged in.
- **Bench test:** power the DRV8833 VCC from 6 V (never 12 V, the limit is 10.8 V). Keep the XIAO on USB and share GND.

### 2.3 Firmware (folder `firmware/robot_face/`)
| File | Purpose |
|---|---|
| `robot_face.ino` | Main sketch: behaviours, modes, commands, web page, WiFi, serial |
| `config.h` | **All settings**: pins, motor tuning, sleep, time zone, location, robot hotspot |
| `secrets.h` | WiFi names and passwords. **Git-ignored**: create it by copying `secrets.example.h` |
| `motors.h` | `Drive` class: PWM on the IN pins, smooth speed changes, minimum and maximum PWM, start kick |
| `weather.h` | Background task: finds the location from the internet address (ip-api.com), gets the weather (Open-Meteo, no key needed) and sets the time zone |
| `clock_screen.h` | Sleep screen: date, big clock, weather icon, burn-in protection |
| `FluxGarage_RoboEyes.h` | Animated eyes library (GPLv3, transcribed from the original image) |
| `tools/laptop_control/serial_control.html` | Laptop USB control page (Web Serial, Chrome/Edge) |

**Modes:**
- REST: stands still, eyes look around
- BEHAVIOR: plays a sequence of wheel moves and expressions
- MANUAL: driven by a controller
- SLEEP: dimmed clock, date and weather

Without commands for `SLEEP_AFTER_MIN` minutes the robot falls asleep; any command wakes it.

**Behaviours:** Hello, Dance, Explore, Angry, Scared, Dizzy, Sleepy, No!, plus Wake up and Fall asleep. Each step sets wheel speeds, mood, where the eyes look, and effects: blink, laugh, confused, sweat, curious, shiver, eyes closed.

**Burn-in protection on the sleep screen:**
- lowest brightness
- the layout moves a few pixels every 60 s
- blinking colon
- screen off from 23:00 to 07:00
- the eyes keep moving while awake

### 2.4 Ways to control it
| Method | How |
|---|---|
| Same WiFi | `http://roboface.local` or the IP shown on the OLED |
| Robot's own WiFi | Join `RoboFace` (password `robot1234`), open `http://192.168.4.1` |
| USB, laptop | `tools/laptop_control/serial_control.html` in Chrome/Edge: Connect, then WASD |
| USB, Serial Monitor | 115200 baud, line ending "New Line", type `help` |

**Commands** (the same for every method):
- **Driving:**
  - `w / a / s / d`
  - `forward 2000` (also `back`, `left`, `right`, time in ms)
  - `x` or `stop`
  - `speed 60`
- **Behaviours and mood:**
  - `1–8` or `dance`, `hello`, …
  - `mood happy` (also `normal`, `angry`, `tired`)
  - `sleep`, `wake`, `z`
  - `auto on`, `auto off`, `m`
- **Motor tuning:** `test`, `minpwm 80`, `maxpwm 255`, `kick 80`
- **Information:** `status`, `wifi` (scans 2.4 GHz networks), `help`

### 2.5 Arduino IDE setup and upload (v1)
1. **Board package:** esp32 by Espressif **3.3.3**. Decline the IDE's update prompt for now; the update is a huge download, and the sketch is tested with 3.3.3.
2. **Library:** Adafruit SSD1306 (choose "Install All" for GFX and BusIO).
3. **WiFi:** copy `firmware/robot_face/secrets.example.h` to `secrets.h` in the same folder and fill in the 2.4 GHz WiFi name and password.
4. **Open:** `firmware/robot_face/robot_face.ino` (all the `.h` files open as tabs).
5. **Tools menu:**

   | Setting | Value |
   |---|---|
   | Board | **XIAO_ESP32C3**. The IDE auto-selects "ESP32 Family Device", which fails with just `exit status 1` |
   | Port | the XIAO's COM port (COM3 and COM4 are Bluetooth) |
   | USB CDC On Boot | **Enabled** |
   | Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |

6. **Upload.** If it fails: hold BOOT, tap RESET, release BOOT, upload again, then press RESET.

### 2.6 Planned v1 audio (optional, if staying on the C3)
| XIAO pin | INMP441 mic | MAX98357 amp |
|---|---|---|
| D7 (GPIO20) | SCK | BCLK |
| D6 (GPIO21) | WS | LRC |
| D10 (GPIO10) | SD | — |
| D8 (GPIO8) | — | DIN |
| 3V3 / 5V | VDD (3V3), L/R → GND | VIN (battery / 5V) |

- The C3 has a **single** I2S unit, so the mic and amp share the clocks and must use the same sample rate (16 kHz).
- A full chatbot can't run on the ESP32-C3 (about 320 KB RAM). Use the cloud instead (see Phase 4).

### 2.7 v1 status and to-do
- [x] Animated eyes, behaviours, sleep clock, weather, automatic location (Ithaca, NY)
- [x] Web, WiFi, serial and USB laptop control
- [x] Firmware compiles (39% of flash with the Huge APP partition)
- [x] First CAD version of the frame (section 2.8)
- [ ] Router 2.4 GHz band: the home router was only broadcasting on 5 GHz. Check with the `wifi` command
- [ ] Print the frame and test-fit the motors, OLED, XIAO, DRV8833 and battery
- [ ] Replace the jammed motor(s) with N20 6 V 50–100 RPM
- [ ] Wire the DRV8833 (with 1 kΩ resistors on the IN wires) and run `test`
- [ ] Battery: 1S LiPo + switch + diode + capacitor
- [ ] Replace the XIAO ESP32-C3 damaged during the 6 V/12 V bench test

### 2.8 Mechanical (CAD): v1 frame
First version of the frame, designed for the XIAO ESP32-C3, two N20 motors and the 0.96" OLED. Files are in `cad/`:

| File | Part | Bounding box (X × Y × Z, as exported) |
|---|---|---|
| `RobotTOP.stl` | Top shell / body | 56.4 × 29.5 × 34.0 mm |
| `RobotBOTTOM.stl` | Bottom plate | 56.4 × 8.1 × 34.0 mm |
| `RobotWheelConnector.stl` | Wheel connector (motor shaft → wheel) | 11.8 × 4.8 × 8.0 mm |
| `qh229_RobotWheelConnector.gcode.3mf` | Sliced print file for the wheel connector | — |

- The top and bottom share the same 56.4 × 34.0 mm footprint. Stacked, the body is about 37.6 mm tall (29.5 + 8.1), not counting the wheels.
- The battery bay target is **26 × 50 × 5 mm** (a 402540 1S LiPo, 4.0 × 25 × 40 mm, fits with room for the wires).

**To check after printing:**
- [ ] The OLED window lines up with the 0.96" screen's active area, and the screen is held securely
- [ ] The N20 motors clamp straight, and the wheel connector fits the motor's D-shaft without slipping
- [ ] There's room for the DRV8833, the switch, the diode and capacitor, and the wire routing
- [ ] USB-C on the XIAO can be reached for charging and uploads without opening the frame
- [ ] The robot sits level and doesn't tip forward or back (add a skid or ball caster if needed)

**For v2 (S3 AMOLED board):** a new top shell will be needed, with a round window for the 1.43" screen and room for the ~40 mm board, the speaker and the mic hole.

---

## 3. v2: ESP32-S3 AMOLED touch robot (target)

### 3.1 Hardware per robot
| Part | Model | Notes |
|---|---|---|
| Main board | **Waveshare ESP32-S3-Touch-AMOLED-1.43** (also sold as UeeKKoo) | ESP32-S3R8: 240 MHz dual core, 16 MB flash, 8 MB PSRAM, WiFi + BLE 5 |
| Display | Built in: 1.43" round AMOLED 466×466, SH8601 driver (some versions CO5300), QSPI | True black, 16.7M colours. ⚠️ AMOLED can burn in |
| Touch | Built in: FT3168 capacitive | Petting, tapping, drawing |
| IMU | Built in: QMI8658 6-axis | Picked up, shaken, tilted, bumped; accurate turns |
| Clock (RTC) | Built in: PCF85063 (optional coin cell on the SH1.0 connector) | Keeps time without WiFi |
| Battery | 1S LiPo 3.7 V, 402540, **PH1.25 / MX1.25 plug**, check polarity | Charged by the board over USB-C |
| Motor driver | DRV8833 | VCC from the board's VBAT pad |
| Motors | 2× N20 6 V, 50–100 RPM | |
| Microphone | INMP441 I2S | |
| Speaker | MAX98357A I2S amp + 8 Ω 1 W speaker | |
| Storage | microSD slot (built in) | Sounds, music, voice messages |

The board has no camera. Camera features were dropped on purpose.

### 3.2 Board pins (from the Waveshare wiki)
| Function | GPIO |
|---|---|
| Display QSPI (CS, CLK, D0–D3) | 9, 10, 11, 12, 13, 14 |
| Display reset / enable | 21 / 42 |
| Touch + IMU + RTC (I2C SDA/SCL) | 47 / 48 |
| IMU interrupt / RTC interrupt | 8 / 15 |
| SD card (CS, MOSI, MISO, SCLK) | 38, 39, 40, 41 |
| Battery voltage (ADC) | 4 |
| BOOT button | 0 |
| UART header | 43 / 44 |
| USB | 19 / 20 |
| **Listed as free** | **1, 2, 3, 5, 6, 7, 16, 17, 18** (avoid 45/46, which the chip checks at startup) |

### 3.3 Planned add-on wiring (confirm against the header labels and schematic first)
| Add-on | Signal | GPIO |
|---|---|---|
| DRV8833 | IN1, IN2 (left) | 1, 2 |
| DRV8833 | IN3, IN4 (right) | 3, 5 |
| INMP441 + MAX98357 | BCLK (shared) | 6 |
| INMP441 + MAX98357 | WS / LRC (shared) | 7 |
| MAX98357 | DIN | 16 |
| INMP441 | SD | 17 |
| DRV8833 | ULT (fault), optional | 18 |

- The expansion header has a **1.27 mm** pitch: use an adapter to 2.54 mm, or fine wires.
- Check the board's size (about 40 mm) against the robot shell before ordering both.

### 3.4 Software stack for v2
- **Display:** Arduino_GFX (SH8601/CO5300 QSPI) with a full-screen drawing buffer in PSRAM. RoboEyes is ported to colour and 466×466.
- **Touch:** FT3168 driver. **IMU:** SensorLib (QMI8658). **RTC:** PCF85063.
- **Audio:** ESP_I2S. **Offline speech:** Espressif ESP-SR, with the WakeNet wake word and MultiNet command phrases (S3 only).
- **Network:** WiFi setup hotspot, MQTT over TLS, HTTPS client for the voice server.

---

## 4. System architecture

```
 My phone (web app) ──┐                                   ┌── Partner's phone (web app)
                      ├──► MQTT message service (cloud) ◄─┤
 My robot ────────────┘    HiveMQ Cloud / EMQX (free)      └── Partner's robot
      │                                                          │
      └───────────── HTTPS ──► Voice/AI server ◄── HTTPS ────────┘
                               speech-to-text → AI model (e.g. Claude)
                               → text-to-speech + emotion/move tags
                               (holds all API keys; robots and the app never do)
```

### 4.1 MQTT topics
```
pair/<pairId>/<robotId>/status     retained: online/offline (last will), battery, mood
pair/<pairId>/<robotId>/event      things that happened: touched, picked_up, shaken, heard_name
pair/<pairId>/<robotId>/cmd        commands TO this robot: from its owner's app or the partner's robot
pair/<pairId>/<robotId>/media      links to voice messages, sounds, music, drawings
```

### 4.2 Example messages
```json
{ "type": "hug",   "from": "robotA" }
{ "type": "mood",  "value": "happy" }
{ "type": "face",  "name": "silly_tongue", "ms": 3000 }
{ "type": "sound", "name": "fart", "volume": 60 }
{ "type": "music", "url": "https://.../song.mp3" }
{ "type": "voice", "url": "https://.../msg123.wav", "from": "me" }
{ "type": "dance", "name": "Dance", "startAt": 1761600000 }
{ "type": "draw",  "points": [[120,80],[121,82]], "color": "#ff4080" }
```

### 4.3 Security rules
- A separate MQTT login per robot and per phone, each limited to `pair/<pairId>/#`.
- TLS always (port 8883 on the robots, secure WebSocket in the app).
- No admin logins or AI keys in the firmware or the web app. Only the voice server holds AI keys.
- The microphone only sends audio after the wake word, or while a button is held, with an on-screen icon.

---

## 5. Upgrade roadmap

Each phase ends with something that works and that you can show off. Don't start a phase until the one before works reliably.

### Phase 0: finish the v1 robot (ESP32-C3)
**Goal:** one robot that drives, shows emotions and is controlled from the phone. *(Mostly done.)*
- [ ] New N20 motors, DRV8833, LiPo power (section 2.7)
- [ ] Tune `MOTOR_MIN_PWM` / `MOTOR_MAX_PWM` with `test`, `minpwm` and `maxpwm`
- [ ] Router on 2.4 GHz → clock, weather and `roboface.local` work

**Done when:** it drives all 8 behaviours on battery for at least 30 minutes, controlled from the phone.

### Phase 1: move to the ESP32-S3 AMOLED board
**Goal:** the same robot, with a colour touch face and motion sensing.
1. Bring up the board with the Waveshare demos: display, touch, IMU, RTC, battery reading.
2. Port the code:
   - [ ] **Display:** RoboEyes on an Arduino_GFX full-screen buffer, 466×466, **colour by mood** (blue normal, red angry, pink happy, grey tired)
   - [ ] **Clock:** the clock and weather screen in colour, with burn-in protection kept (AMOLED)
   - [ ] **Motors and settings:** DRV8833 on GPIO 1/2/3/5; `config.h` updated for the S3
   - [ ] **Kept as they are:** behaviours, commands, web page, serial control
3. New hardware features:
   - [ ] **Touch:** stroke = happy and purr-eyes, tap = blink, long press = annoyed
   - [ ] **IMU:** picked up = surprised and wheels stop; shaken = dizzy; tipped over = "help!"; bump = back off; accurate 90°/180° turns
   - [ ] **RTC:** clock without WiFi. **Battery:** percentage on the clock screen and low-battery sleepy eyes

**Done when:** the v2 robot does everything v1 did, plus touch and motion reactions.

### Phase 2: setup page, partner link and app v1
**Goal:** both robots online, each controllable from both phones, and reacting to each other.
1. **WiFi setup hotspot:** the robot creates `RoboFace-Setup`, and a phone page sets the WiFi, robot name, pair ID and MQTT login. Your partner never needs Arduino IDE.
2. **MQTT client in the firmware:** online status with a last-will message, a retained mood, and command and event topics.
3. **Web app v1:** a page added to the phone's home screen, hosted free (GitHub Pages / Netlify), using mqtt.js. It can:
   - drive **my** robot, and send to the **partner's** robot (hug, mood, behaviours)
   - show both robots: online/offline, mood, battery
4. **Robot-to-robot interactions:**
   - **Send a hug:** pet your robot's screen for 2 s, and the partner's robot shows hearts and wiggles
   - **Mood sync:** a small icon shows the partner robot's mood
   - **"I'm home" / "Good night":** the partner's robot greets them or goes to sleep too
   - **Dance together:** a synchronised dance using a shared start time
   - **Meeting in person:** ESP-NOW detects the other robot nearby, and they greet each other

**Done when:** a hug sent from either phone or robot reaches the other robot in under 2 seconds, across different WiFi networks.

### Phase 3: microphone and speaker
**Goal:** the robots make sounds and react to sound, with everything still working offline.
- [ ] Wire the INMP441 and MAX98357 (section 3.3), at 16 kHz with shared clocks
- [ ] **Sound effects made in code** for each behaviour: chirps, growl, yawn, giggle, beeps while driving
- [ ] **Volume and mute** in the app
- [ ] **Clap or loud-sound reactions:** a clap wakes it, a loud bang makes it scared
- [ ] **Offline voice (ESP-SR):**
  - a wake word, for example "Hi ESP" (a custom wake word needs Espressif training)
  - about 20–50 command phrases: "dance", "go to sleep", "send a hug", "come here", "how are you"
- [ ] **Voice messages:** hold to record in the app or on the robot; upload; the partner's robot plays it; stored on the SD card

**Done when:** "Hi ESP, send a hug" works with no cloud AI, and voice messages play on the partner's robot.

### Phase 4: cloud AI conversation
**Goal:** a natural conversation, with the robot acting out its replies.
1. **Voice server** (small cloud host or a Raspberry Pi; holds all API keys):
   - receives an audio clip, then speech-to-text, then the AI model (for example Claude), then text-to-speech
   - returns the reply audio, the text, and **action tags** such as `[happy] [dance] [look_left]`
2. **The robot:**
   - after the wake word, records until silence and sends the clip
   - plays the reply while the face lip-syncs (mouth or eyes bouncing with the volume) and acts out the tags
3. **Personality prompt:** each robot gets its own name, personality and short memory (owner names, shared jokes, the partner robot's name).
4. **Shared events:** messages from the partner robot become AI context, so the robot can say something like "Your partner sent you a hug an hour ago!"

**Done when:** a 5-turn conversation in which the robot's emotions and movements match what it says, with replies in under 5 s.

### Phase 5: multimodal interaction (voice + touch + IMU → AI)
**Goal:** the robot understands how it's being treated physically, not just what is said.
1. **Event layer on the robot:** it turns sensor data into short labelled events:
   - **touch:** petted (stroke), poked (tap), held (long press), and where on the screen
   - **IMU:** picked up, carried, shaken, dropped, tilted, spun, bumped into something, set down
   - **voice:** heard its name, someone talking, a loud noise, silence for a long time
   - **context:** time of day, battery level, partner robot online, last interaction time
2. **Two-level response:**
   - **Reflexes run on the robot instantly** (no cloud): it flinches when poked, gets dizzy when shaken, and is surprised when picked up.
   - **The AI gets a summary of recent events** with each request, for example "was petted gently 3 times, then picked up". Its reply reflects that ("That tickles!", "Put me down!").
3. **Simple emotional state:**
   - happiness, energy and loneliness values change with interactions and slowly drift back over time
   - they set the idle face, the behaviour choice and the AI's tone
   - a robot left alone too long gets lonely and "calls" its owner or the partner robot
4. **Shared between robots:** important events go to the partner's robot over MQTT ("your owner just petted me").

**Done when:** the robot reacts sensibly to petting, poking, being picked up and shaking, both instantly and in conversation.

### Phase 6: fun app features
**Goal:** playful ways to reach your partner through their robot.
- [ ] **Funny faces:** a gallery (tongue out, cross-eyed, heart eyes, crying, sunglasses, sleepy drool, angry steam) sent to the partner's robot for a few seconds
- [ ] **Sound board:** fart, kiss, laugh, "boop", air horn, cat meow; custom sounds you record
- [ ] **Music:** pick a song in the app and it plays on the partner's robot, which dances to the beat (beat detection, or the song's tempo)
- [ ] **Draw on the partner's screen:** draw in the app or on your robot's touch screen, and it appears live on theirs
- [ ] **Little games:** Simon-says with faces; rock-paper-scissors between the two robots; the robots "race" to react
- [ ] **Scheduled surprises:** a good-morning message at 7:00; an anniversary dance; a reminder in the robot's voice
- [ ] **Photo of the moment:** the app shows the partner robot's current face as a live mini-view
- [ ] **Optional native app** (Flutter / React Native): better notifications, Bluetooth setup, app stores (Apple $99/yr, Google $25 once)

**Done when:** both of you use at least one fun feature daily without needing help.

---

## 6. Safety rules and lessons learned
| Lesson | Rule from now on |
|---|---|
| A motor overheated while idle: the Seeed demo left the motor pins floating | Flash the robot firmware **before** connecting motor power. Unplug motor power while uploading |
| A motor seized: stalled for a long time, now jammed | Never leave motors powered while stalled. Test each new motor alone at its rated voltage first |
| The XIAO ESP32-C3 died during the bench test with the L298N and a 6/12 V supply | Never use the 12 V setting on N20 motors or the DRV8833. Check the wiring before power-on. Use 1 kΩ resistors on the IN wires. Test the driver without the microcontroller connected |
| ER14250 lithium battery on a Li-ion charger | **Never charge ER/primary lithium cells.** Only rechargeable 1S LiPo or Li-ion (LiPo, ICR, LIR) |
| Battery on the 5V pin while USB is plugged in | Use a Schottky diode or a switch; never connect both without one. On v2, use the board's own battery connector |
| L298N with 3–4 V motors | Use a DRV8833 for small motors and 1S batteries |
| The router only broadcast on 5 GHz | The ESP32 needs **2.4 GHz** WiFi. Use the `wifi` command to check what the robot sees |
| Compile failed with only `exit status 1` | Make sure the board is **XIAO_ESP32C3** (or the correct S3 board), not "ESP32 Family Device" |
| OLED/AMOLED burn-in | Keep the eyes moving, dim while asleep, screen off at night |
| Credentials | WiFi and MQTT logins only in the git-ignored `secrets.h`; AI keys only on the server. Never commit them |

---

## 7. Budget (approximate, per robot)
| Item | v1 (C3) | v2 (S3 AMOLED) |
|---|---|---|
| Main board | $5 (XIAO ESP32-C3) | $36 (AMOLED touch board) |
| Display | $3 (from the 3-pack) | built in |
| DRV8833 | $3 | $3 |
| 2× N20 6 V motors + wheels | $10 | $10 |
| 1S LiPo + switch + diode + capacitors | $8 | $8 |
| INMP441 + MAX98357 + speaker | ($10, optional) | $10 |
| **Total** | **about $29–39** | **about $67** |

**Running costs:**
- MQTT, web app hosting and weather: **free tiers**
- Cloud AI voice (Phase 4+): **a few cents per conversation**
- App stores: only if publishing native apps

---

## 8. Open questions
- [ ] The robot shell size: does the ~40 mm AMOLED board fit the face? Is the 26 × 50 × 5 mm battery bay still available?
- [ ] The exact pinout of the S3 board's 1.27 mm header (from a photo or the schematic) before wiring
- [ ] SH8601 or CO5300 display driver on the boards received
- [ ] MQTT service choice (HiveMQ Cloud or EMQX) and the pair ID
- [ ] Where to run the voice server (cloud function, small VPS, or a Raspberry Pi)
- [ ] Robot names and personalities for both robots
- [ ] Custom wake word ("Hey <robot name>") or the built-in "Hi ESP"

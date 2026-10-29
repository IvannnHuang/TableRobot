# Wheelbot: XIAO ESP32-C3 wheeled robot (RoboFace v1)

A small two-wheeled desk robot with an animated OLED face. It drives, shows emotions, keeps a sleep clock with weather, and is controlled from a phone, a laptop or over USB.

- **Status: finished.** This is the first robot of the TableRobot repo. It stays as it is; new work goes into the robot dog (`../robotdog/`), which grew out of the v2 ideas that used to be in this plan.
- Board: Seeed XIAO ESP32-C3 + 0.96" OLED + DRV8833 + 2× N20 motors.

Last updated: 2026-10-29

---

### Folder layout
```
wheelbot/
├── CLAUDE.md                       context for Claude Code sessions on this robot
├── README.md                       quick start
├── docs/WHEELBOT_PLAN.md           this file
├── firmware/robot_face/            Arduino sketch
│   ├── robot_face.ino
│   ├── config.h                    settings (pins, motors, sleep, location)
│   ├── secrets.example.h           template → copy to secrets.h (git-ignored) for WiFi passwords
│   ├── motors.h, weather.h, clock_screen.h, FluxGarage_RoboEyes.h
├── tools/laptop_control/serial_control.html   USB control page (Chrome/Edge)
└── cad/                            frame: STL parts + sliced print files
```

---

## Contents
1. [Goals](#1-goals)
2. [Hardware, firmware and control](#2-v1-xiao-esp32-c3-robot)
3. [Safety rules and lessons learned](#3-safety-rules-and-lessons-learned)
4. [Budget](#4-budget)

---

## 1. Goals
- A face that clearly shows emotions: happy, angry, tired, curious, scared, sleepy.
- Body language through the wheels: dance, nod, shake "no", turn toward you.
- Control from a phone or laptop: on the same WiFi, on the robot's own WiFi, or over USB.
- Useful desk features: sleep clock with weather, messages, focus timer, alarm, reminders.

---

## 2. v1: XIAO ESP32-C3 robot

### 2.1 Hardware
| Part | Model | Notes |
|---|---|---|
| Microcontroller | Seeed XIAO ESP32-C3 | 2.4 GHz WiFi only, 4 MB flash, 11 GPIO |
| Display | ELEGOO 0.96" SSD1306 128×64 I2C, address 0x3C | Two-colour: top 16 rows yellow, bottom 48 rows blue (fixed by the panel). Monochrome pixels |
| Motor driver | ~~L298N~~ → **DRV8833** | The L298N loses 2–3 V, so the motors only hummed. The DRV8833 works from a 3.7 V battery and has overcurrent protection |
| Motors | 2× N20 (1218GE) gear motor, 16 RPM listing (3 V/6 V versions mixed) | ⚠️ The tested motor is **mechanically jammed**: 20 Ω coil, draws the full stall current (≈0.3 A at 6 V), doesn't turn. Replace with **N20 6 V, 50–100 RPM** |
| Battery | ~~ER14250~~ → **v1: EEMB 402535, 3.7 V 320 mAh (300 min), 1.25 mm 2-pin connector, with protection**. (The EEMB 603449, 1100 mAh, was the old v2 candidate; it is too weak for the robot dog's servos.) | 402535 = 25.5 × 36 × 4.3 mm, fits the 26 × 50 × 5 mm space. Rated about 1C (≈320 mA continuous): keep the 470 µF cap, lower `kick`/`maxpwm` if it browns out. Estimated runtime about 2 h with WiFi on. ER14250 is **not rechargeable** and too weak, so don't use it |

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
EEMB LiPo (1.25 mm plug) ── UMLIFE charger B+/B−
  OUT+ ── switch ──┬── XIAO 5V pin
                   ├── DRV8833 VCC
                   └── 470 µF capacitor + (− to GND), next to the DRV8833
  OUT− ────────────┴── XIAO GND ── DRV8833 GND ── OLED GND
```
- **No diode** (chosen on purpose). Rule: **switch OFF whenever the XIAO's USB-C is plugged in.** Otherwise the laptop's 5 V pushes straight into the LiPo through the charger's OUT+.
- Charge through the **charger's** USB port, never through the XIAO's. Charging works with the switch on or off.
- The UMLIFE board charges at **1 A**: fine for the 1100 mAh cell (0.9C), **too fast for the 320 mAh cell (≈3C)**. For the 320 mAh cell, lower the charge current (PROG resistor ≈4–5 kΩ on a TP4056-type chip, about 250–300 mA) or use a slower charger.
- Running while charging works, but the robot's ~100 mA load can stop the charger from ever seeing "full", so it holds the cell at 4.2 V. Don't leave it running on the charger all the time; switch OFF when charging a flat battery. Keep the charger board away from the cell, because it gets hot at 1 A.
- **Running without a battery:** power it through the **XIAO's** USB-C with the switch OFF, not through the charger board. A TP4056-type charger with no cell attached is not designed as a power supply: depending on the chip and the load, its output may cycle on and off and reset the XIAO. It's harmless to try. For one USB-C port that both charges and powers the robot, add a load-sharing circuit (P-MOSFET + Schottky diode) between the charger's 5 V input and the rail.
- **USB-only testing before the charger is connected:** the XIAO 5V pin feeds the rail, so the DRV8833 VCC gets about 5 V and the motors run without a battery.
- **Bench test:** power the DRV8833 VCC from 6 V (never 12 V, the limit is 10.8 V). Keep the XIAO on USB and share GND.
- Check the battery's polarity before plugging it into the charger: JST wire colours are not standard.

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

**Behaviours:** Hello, Dance, Explore, Angry, Scared, Dizzy, Sleepy, No!, and 9 = My dance (recorded from the phone), plus Wake up and Fall asleep. Each step sets wheel speeds, mood, where the eyes look, and effects: blink, laugh, confused, sweat, curious, shiver, eyes closed.

**Personality** (with `mood auto`, the default):
- happy eyes 7–10, tired eyes from 22:00
- sweats when it's hot outside, shivers now and then when it's cold (from the weather)
- after 2 minutes alone it looks up, wiggles and sighs to ask for attention; yawns at night

**Notes and timers:**
- **Messages:** text from the phone shows on the OLED (in turns with the eyes) until "seen"; `<3` is a heart. Sleeping, it shows an envelope on the clock
- **Focus timer:** stays quiet, shows the time left now and then, dances when it's over, then a 5 min break
- **Alarm:** every day; wakes up, dances every 20 s and shows the time for 3 minutes or until stopped
- **Reminders:** up to 4 repeating ones (e.g. water every 60 min), skipped at night
- Alarm, reminders and My dance survive a restart (saved in flash with `Preferences`)

**Power saving:** CPU at 80 MHz; the robot WiFi switches off 3 min after the home WiFi connects if no phone is on it (back on when the home WiFi is lost, or `ap on`); the WiFi radio naps while no page is open; the CPU rests while asleep.

**Lag fixes:** home WiFi attempts pause while a phone is on the robot WiFi (each attempt scans all channels), and the weather download waits while someone is driving (the C3 has one core). `status` shows the slowest loop; the phone page shows the round-trip time.

**Updates over WiFi:** after flashing once over USB with the OTA partition (2.5), Arduino IDE lists the robot under Tools > Port as `roboface`. The password is `OTA_PASS` in `secrets.h`, or the robot WiFi password.

**Burn-in protection on the sleep screen:**
- dimmed (`SLEEP_CONTRAST`)
- the layout moves a few pixels every 60 s
- blinking colon
- 23:00 to 07:00: only a small clock, in a new spot every minute (or fully off with `NIGHT_SMALL_CLOCK false`)
- the eyes keep moving while awake; message and WiFi cards shift a few pixels too

### 2.4 Ways to control it
| Method | How |
|---|---|
| Same WiFi | `http://roboface.local` or the IP shown on the OLED. Phone remote for holding the phone upright, with three tabs at the bottom: **Drive** (joystick, speed, stop, record a dance), **Play** (behaviours, mood, sleep, wander) and **Notes** (message, focus timer, alarm, reminders). Add it to the home screen to open it fullscreen like an app. After start-up the address stays on the OLED until the page is opened (at most `WIFI_INFO_MAX_MIN`) |
| Robot's own WiFi | Join `RoboFace` (password `robot1234`), open `http://192.168.4.1` |
| USB, laptop | `tools/laptop_control/serial_control.html` in Chrome/Edge: Connect, then WASD |
| USB, Serial Monitor | 115200 baud, line ending "New Line", type `help` |

**Commands** (the same for every method):
- **Driving:**
  - `w / a / s / d`
  - `forward 2000` (also `back`, `left`, `right`, time in ms)
  - `joy 30 80`: joystick, turn and forward from -100 to 100, mixed into curves (sent by the phone page)
  - `x` or `stop`
  - `speed 60`
- **Behaviours and mood:**
  - `1–8` or `dance`, `hello`, …; `9` = My dance
  - `mood auto` (default, follows time and weather; also `normal`, `happy`, `angry`, `tired`)
  - `sleep`, `wake`, `z`
  - `auto on`, `auto off`, `m`
- **Notes and timers:**
  - `msg Miss you <3`, `seen`
  - `focus 25`, `focus stop`
  - `alarm 7:30` (also `7:30pm`, `19:30`), `alarm off`, `alarm stop`
  - `remind 60 Drink water`, `remind list`, `remind off Drink water`
- **My dance:** `rec`, `rec stop`, `rec play` (or `9`), `rec delete`
- **Motor tuning:** `test`, `minpwm 80`, `maxpwm 255`, `kick 80`
- **Information:** `status`, `wifi` (scans 2.4 GHz networks), `settime`, `help`
- **Power:** `ap on` (robot WiFi stays on), `ap off`, `ap auto`

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
   | Partition Scheme | **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)** (needed for updates over WiFi) |

6. **Upload.** If it fails: hold BOOT, tap RESET, release BOOT, upload again, then press RESET.
7. **Later updates over WiFi:** with the robot on the same network as the laptop, pick Tools > Port > `roboface at 192.168…`, then Upload. The password is `OTA_PASS` from `secrets.h`, or `AP_PASS` if that isn't set. Lift the wheels or switch the motors off first: the robot keeps running until the update starts.

### 2.6 Planned v1 audio (optional, if staying on the C3)
| XIAO pin | INMP441 mic | MAX98357 amp |
|---|---|---|
| D7 (GPIO20) | SCK | BCLK |
| D6 (GPIO21) | WS | LRC |
| D10 (GPIO10) | SD | — |
| D8 (GPIO8) | — | DIN |
| 3V3 / 5V | VDD (3V3), L/R → GND | VIN (battery / 5V) |

- The C3 has a **single** I2S unit, so the mic and amp share the clocks and must use the same sample rate (16 kHz).
- A full chatbot can't run on the ESP32-C3 (about 320 KB RAM). AI conversation is planned for the robot dog instead, through a server.

### 2.7 v1 status and to-do
The owner marked the robot finished on 2026-10-29. The unticked items below were not re-checked when the project was closed; tick them if they were done.

- [x] Animated eyes, behaviours, sleep clock, weather, automatic location (Ithaca, NY)
- [x] Web, WiFi, serial and USB laptop control
- [x] Firmware compiles (66% of the 1.9 MB app space with the Minimal SPIFFS OTA partition)
- [x] Software features: messages, focus timer, alarm, reminders, personality, dance recording, power saving, updates over WiFi
- [ ] Test the new features on the robot (first flash with the new partition over USB)
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

The robot dog has its own body design; see `../robotdog/docs/ROBOTDOG_PLAN.md`.

---

## 3. Safety rules and lessons learned
| Lesson | Rule from now on |
|---|---|
| The speed slider barely changed the wheel speed | PWM on one input with the other low (fast decay) lets a lightly loaded motor coast up to near full speed. Use slow decay (`MOTOR_SLOW_DECAY`): one input high, inverted PWM on the other |
| The sleep clock showed no time on the robot's own WiFi | No internet means no NTP. The phone page and the USB page now send the time with `settime` when they connect, and the phone page sends it again whenever `/status` reports `"clock":false` (robot restarted while the page stayed open). A WiFi screen waiting for a phone no longer covers the clock |
| A motor overheated while idle: the Seeed demo left the motor pins floating | Flash the robot firmware **before** connecting motor power. Unplug motor power while uploading |
| A motor seized: stalled for a long time, now jammed | Never leave motors powered while stalled. Test each new motor alone at its rated voltage first |
| The XIAO ESP32-C3 died during the bench test with the L298N and a 6/12 V supply | Never use the 12 V setting on N20 motors or the DRV8833. Check the wiring before power-on. Use 1 kΩ resistors on the IN wires. Test the driver without the microcontroller connected |
| ER14250 lithium battery on a Li-ion charger | **Never charge ER/primary lithium cells.** Only rechargeable 1S LiPo or Li-ion (LiPo, ICR, LIR) |
| Battery on the 5V pin while USB is plugged in | Use a Schottky diode or a switch; never connect both without one. (The robot dog's S3 board has its own charger and USB/battery switching.) |
| L298N with 3–4 V motors | Use a DRV8833 for small motors and 1S batteries |
| The router only broadcast on 5 GHz | The ESP32 needs **2.4 GHz** WiFi. Use the `wifi` command to check what the robot sees |
| Compile failed with only `exit status 1` | Make sure the board is **XIAO_ESP32C3** (or the correct S3 board), not "ESP32 Family Device" |
| OLED/AMOLED burn-in | Keep the eyes moving, dim while asleep, screen off at night |
| Credentials | WiFi and MQTT logins only in the git-ignored `secrets.h`; AI keys only on the server. Never commit them |

These rules carry over to the robot dog.

---

## 4. Budget
| Item | Cost |
|---|---|
| Main board | $5 (XIAO ESP32-C3) |
| Display | $3 (from the 3-pack) |
| DRV8833 | $3 |
| 2× N20 6 V motors + wheels | $10 |
| 1S LiPo + switch + diode + capacitors | $8 |
| INMP441 + MAX98357 + speaker | ($10, optional) |
| **Total** | **about $29–39** |

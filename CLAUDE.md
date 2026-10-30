# TableRobot: notes for Claude Code

Two small two-wheeled desk companion robots (one for the owner, one for their partner) with an animated face, wheels, and, later, voice, AI and robot-to-robot interaction. **The full plan, hardware details, wiring, roadmap and lessons learned are in `docs/ROBOT_PLAN.md`; read it first.** Keep it up to date when hardware, pins or progress change.

## Current state
- v1 = Seeed **XIAO ESP32-C3** + ELEGOO 0.96" SSD1306 OLED (128×64, I2C 0x3C, yellow top 16 rows / blue bottom) + **DRV8833** + 2× N20 motors. The firmware is done; the hardware is being rebuilt (new motors, DRV8833, LiPo, replacement XIAO).
- v2 target = **Waveshare ESP32-S3-Touch-AMOLED-1.43** (466×466 AMOLED, touch, QMI8658 IMU, RTC) + DRV8833 + INMP441 mic + MAX98357 amp. Camera features were dropped on purpose.
- The roadmap phases (0–6) are in `docs/ROBOT_PLAN.md` section 5.
- **Under evaluation (2026-10-30):** a walking **Microduck** biped (14 Dynamixel XL330 servos) on an ESP32-S3 instead of the Pi, with a touch screen + camera head instead of the duck head. Analysis only. Findings, servo prices, risks and pending decisions are in `docs/ROBOT_PLAN.md` **section 9**.
  - The owner wants analysis before changes: don't change firmware, order parts or build for this until they decide (9.8).
  - `Microduck-build-tutorial/` is a third-party GPL-3.0 clone with its own `.git`, untracked in this repo. Read it as reference, don't edit it, and don't `git add` it.
  - Key facts:
    - The policy is a 51→512→256→128→14 MLP at 50 Hz, and the neck/head joints are part of it, so a new head means retraining.
    - The clone's training robot model was deleted (get it from upstream `pollen-robotics/microduck`).
    - Hobby servos can't run the policy (no feedback). An S3 can run it (int8 weights); a C3 can't (no FPU).

## Firmware (`firmware/robot_face/`)
- Arduino sketch. The folder name must match the `.ino`. Settings are in `config.h`; WiFi credentials are in the **git-ignored `secrets.h`** (template `secrets.example.h`). **Never commit secrets or print them in chat.**
- `FluxGarage_RoboEyes.h` is a third-party GPLv3 library (transcribed). Don't restyle it; patch minimally.
- Include RoboEyes **last**: its short macros (`N`, `E`, `S`, `W`, `ON`, `OFF`, `DEFAULT`) clash with other headers. `#undef DEFAULT` comes before it because the ESP32 core defines `DEFAULT` too.
- Serial, web and USB control all go through `runCommand()`. Add new commands there, and update `HELP_TEXT` and `tools/laptop_control/serial_control.html` (the behaviour list is duplicated there).
- Keep `loop()` non-blocking: no long `delay()`s. Network calls run in the background task in `weather.h`. The C3 has one core, so that task waits (`weatherHold`) while someone is driving.
- Alarm, reminders and the recorded dance are saved with `Preferences` (namespace `robot`).
- Code style: match the existing files. Section banners `//****`, short comments, descriptive names.

## Build (verify every change compiles)
ESP32 core **3.3.3** is installed; don't upgrade without testing. The arduino-cli that comes with Arduino IDE:
```
"/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe" \
  --config-file ~/.arduinoIDE/arduino-cli.yaml compile \
  --fqbn esp32:esp32:XIAO_ESP32C3:CDCOnBoot=cdc,PartitionScheme=min_spiffs \
  --build-path <scratch dir> firmware/robot_face
```
- Partition `min_spiffs` (1.9 MB app with OTA) is needed for updates over WiFi (`ArduinoOTA`, port `roboface`). The sketch must stay under 1.9 MB.
- Raw-string web pages (`R"HTML(...)HTML"`): avoid a bare `'` in the HTML text (use `&#39;`). An odd number of apostrophes confuses the Arduino prototype generator, which then reads the JavaScript `function` lines as C++.
- The IDE auto-selects "ESP32 Family Device" when the board is plugged in, and that fails with just `exit status 1`. The board must be **XIAO_ESP32C3**.
- The owner uploads to hardware themselves. Don't send motor commands to a connected board without asking (the wheels may not be lifted).

## Hardware rules (learned the hard way)
- Never 12 V on N20 motors or the DRV8833 (max 10.8 V). Motor power must not reach the XIAO pins; use 1 kΩ resistors on the IN wires.
- Flash the firmware before connecting motor power (floating pins left a motor stalled until it overheated).
- No non-rechargeable lithium cells (ER14250) and no charger on primary cells. Use a 1S LiPo with protection, ≥2C.
- A battery on the XIAO 5V pin needs a diode or switch so USB can't back-feed it.
- The ESP32 only does 2.4 GHz WiFi.
- OLED/AMOLED burn-in: keep the eyes moving, dim the sleep screen, only a small moving clock at night. Anything that stays on screen (cards, WiFi info) shifts a few pixels every `PIXEL_SHIFT_SEC`.

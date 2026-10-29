# Wheelbot: notes for Claude Code

The finished two-wheeled desk robot (RoboFace v1). **The plan, hardware details, wiring and lessons learned are in `docs/WHEELBOT_PLAN.md`; read it first.** This project is separate from the robot dog in `../robotdog/`: don't share code or files between them unless the owner asks.

## Current state
- Seeed **XIAO ESP32-C3** + ELEGOO 0.96" SSD1306 OLED (128×64, I2C 0x3C, yellow top 16 rows / blue bottom) + **DRV8833** + 2× N20 motors.
- The owner marked it finished on 2026-10-29. Changes now are fixes or small additions; keep the plan up to date when they touch hardware, pins or features.

## Firmware (`firmware/robot_face/`)
- Arduino sketch. The folder name must match the `.ino`. Settings are in `config.h`; WiFi credentials are in the **git-ignored `secrets.h`** (template `secrets.example.h`). **Never commit secrets or print them in chat.**
- `FluxGarage_RoboEyes.h` is a third-party GPLv3 library (transcribed). Don't restyle it; patch minimally.
- Include RoboEyes **last**: its short macros (`N`, `E`, `S`, `W`, `ON`, `OFF`, `DEFAULT`) clash with other headers. `#undef DEFAULT` comes before it because the ESP32 core defines `DEFAULT` too.
- Serial, web and USB control all go through `runCommand()`. Add new commands there, and update `HELP_TEXT` and `tools/laptop_control/serial_control.html` (the behaviour list is duplicated there).
- Keep `loop()` non-blocking: no long `delay()`s. Network calls run in the background task in `weather.h`. The C3 has one core, so that task waits (`weatherHold`) while someone is driving.
- Alarm, reminders and the recorded dance are saved with `Preferences` (namespace `robot`).
- Code style: match the existing files. Section banners `//****`, short comments, descriptive names.

## Build (verify every change compiles)
ESP32 core **3.3.3** is installed; don't upgrade without testing. Run from the repo root with the arduino-cli that comes with Arduino IDE:
```
"/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe" \
  --config-file ~/.arduinoIDE/arduino-cli.yaml compile \
  --fqbn esp32:esp32:XIAO_ESP32C3:CDCOnBoot=cdc,PartitionScheme=min_spiffs \
  --build-path <scratch dir> wheelbot/firmware/robot_face
```
- Partition `min_spiffs` (1.9 MB app with OTA) is needed for updates over WiFi (`ArduinoOTA`, port `roboface`). The sketch must stay under 1.9 MB.
- Raw-string web pages (`R"HTML(...)HTML"`): avoid a bare `'` in the HTML text (use `&#39;`). An odd number of apostrophes confuses the Arduino prototype generator, which then reads the JavaScript `function` lines as C++.
- The IDE auto-selects "ESP32 Family Device" when the board is plugged in, and that fails with just `exit status 1`. The board must be **XIAO_ESP32C3**.
- The owner uploads to hardware themselves. Don't send motor commands to a connected board without asking (the wheels may not be lifted).

## Hardware rules specific to this robot
- Never 12 V on N20 motors or the DRV8833 (max 10.8 V). Motor power must not reach the XIAO pins; use 1 kΩ resistors on the IN wires.
- A battery on the XIAO 5V pin needs a diode or switch so USB can't back-feed it.
- The shared rules (flash before motor power, LiPo only, 2.4 GHz, burn-in) are in the root `CLAUDE.md`.

# Robot dog: notes for Claude Code

Two four-legged desk dogs (one for the owner, one for their partner) with a round AMOLED face, 4 leg servos + 1 neck servo, mic and speaker. **The plan, board facts, header pinout, pin map, power design and roadmap (D0–D7) are in `docs/ROBOTDOG_PLAN.md`; read it first.** Keep it up to date when hardware, pins or progress change. This project is separate from `../wheelbot/`: don't share code or files with it unless the owner asks.

## Current state
- Board bought: Waveshare **ESP32-S3-Touch-AMOLED-1.43** (ESP32-S3R8, 8 MB PSRAM, 16 MB flash, 466×466 QSPI AMOLED with SH8601 or CO5300, FT3168 touch, QMI8658 IMU, PCF85063 RTC, ETA6098 charger). No audio on board.
- Not the **1.43C** (that variant has audio but no IMU, RTC or SD); don't copy its pins.
- Next step: D0, board bring-up. ESP-IDF v6.0.2 is not installed yet.

## Firmware (`firmware/`, git submodule)
- Fork of tabledog (XiaoZhi), repo `IvannnHuang/tabledog`. **ESP-IDF v6.0.2, C++.** Follow `firmware/AGENTS.md` (board selection chain, `DECLARE_BOARD`, state machine, no blocking the main loop or audio tasks, don't edit generated files, clang-format only touched files).
- It's its own git repo: commit inside `firmware/` first, then commit the new submodule pointer in TableRobot. Commit or push only when the owner asks.
- Add our board as a **new board folder**; never change `caiyu-c3`'s pins (AGENTS.md rule; board identity affects OTA).
- Upstream display/touch code for the AMOLED boards is in `78/xiaozhi-esp32` (`main/boards/waveshare/esp32-s3-touch-amoled-*`), not in the trimmed fork.
- Offline mode is a goal: the dog must work with no AI server (wake word, MultiNet commands, web console, MQTT).
- Device tools (`McpServer::AddTool`) are the one control surface: the web console, MQTT and the AI should all call the same actions.
- Build: `python scripts/build.py <board>` in `firmware/` with ESP-IDF sourced, then `idf.py -p <PORT> flash monitor`. Verify every change builds. A build is not hardware validation; say what still needs testing on the dog.

## Pins and power (details in the plan, sections 2.3–2.5)
- Servos: GPIO 1, 2, 3, 5 (legs LF, RF, LB, RB), 6 (neck). Audio: BCLK 16, WS 17, mic data 7, amp data 18. Spare: 43, 44, and 38–41 without the SD card.
- Avoid GPIO 0, 45, 46. GPIO 8–15, 19–21, 42, 47, 48 are used on the board.
- Servo current goes battery → switch → 5 V boost (≥3 A) → servos, never through the board or its 1.27 mm header pins. Shared ground.
- The board charges at about 2 A (R23 = 82 kΩ) and has no on/off switch.
- Flash before connecting servo power. Don't move servos on a connected dog without asking.

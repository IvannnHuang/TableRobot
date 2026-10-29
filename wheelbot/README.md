# Wheelbot (RoboFace v1)

A small two-wheeled desk robot with an animated OLED face, built on the Seeed XIAO ESP32-C3. You can drive it and change its mood from a phone, a laptop or over USB, and it shows a clock with weather while it sleeps.

**Status: finished.** The follow-up project, a four-legged robot dog, is in [`../robotdog/`](../robotdog/).

The full plan, wiring and lessons learned are in **[docs/WHEELBOT_PLAN.md](docs/WHEELBOT_PLAN.md)**.

## Contents
| Folder | What's inside |
|---|---|
| `firmware/robot_face/` | Arduino firmware (ESP32-C3 + 0.96" OLED + DRV8833 motors) |
| `tools/laptop_control/` | Laptop control page over USB (Chrome/Edge) |
| `cad/` | 3D-printable frame: top shell, bottom plate, wheel, wheel connector |
| `docs/` | Project plan |

## Quick start
1. Install **Arduino IDE**, the **esp32 by Espressif 3.3.3** boards package and the **Adafruit SSD1306** library.
2. Copy `firmware/robot_face/secrets.example.h` to `secrets.h` and enter your **2.4 GHz** WiFi details. `secrets.h` stays out of git.
3. Open `firmware/robot_face/robot_face.ino`.
4. Set these in the Tools menu:
   - Board **XIAO_ESP32C3**
   - USB CDC On Boot **Enabled**
   - Partition Scheme **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)** (needed for updates over WiFi)
5. Upload.
6. Open the Serial Monitor at 115200 baud, choose the "New Line" line ending, and type `help`.
7. Control the robot from a browser at `http://roboface.local`.

The wiring is in [docs/WHEELBOT_PLAN.md, section 2.2](docs/WHEELBOT_PLAN.md#22-wiring-v1-with-drv8833).

## License
- `firmware/robot_face/FluxGarage_RoboEyes.h` is © Dennis Hoelscher, under GPLv3.
- Because the firmware includes it, the firmware as a whole is distributed under GPLv3 too.

# Robot dog

A small four-legged desk dog with a turning neck and a round colour touch screen face, built on the Waveshare ESP32-S3-Touch-AMOLED-1.43. It obeys simple voice commands offline, can be controlled from a phone at home or from anywhere, and two dogs keep each other company over the internet. AI conversation comes last.

**Status: planning; the board is bought.** The full plan (hardware, pinout, power, firmware, networking, roadmap) is in **[docs/ROBOTDOG_PLAN.md](docs/ROBOTDOG_PLAN.md)**.

## Contents
| Folder | What's inside |
|---|---|
| `firmware/` | ESP-IDF firmware, a fork of tabledog (XiaoZhi). Git submodule: [IvannnHuang/tabledog](https://github.com/IvannnHuang/tabledog) |
| `docs/` | Project plan |

If `firmware/` is empty after cloning, run `git submodule update --init` in the repo root.

## Credits
- Idea and firmware base: tabledog (caiyu-dog), based on [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32). MIT license.
- Follow-up to the finished wheeled robot in [`../wheelbot/`](../wheelbot/).

# TableRobot: notes for Claude Code

Desk companion robots, one for the owner and one for their partner. The repo holds **three separate projects**, one per robot version; each has its own `CLAUDE.md` and plan. Read the one for the project you're working on first.

| Folder | Version | Project | Notes |
|---|---|---|---|
| `wheelbot/` | v1 | Finished two-wheeled robot: XIAO ESP32-C3, 0.96" OLED, Arduino | `wheelbot/CLAUDE.md`, `wheelbot/docs/WHEELBOT_PLAN.md` |
| `robotdog/` | v2 | Robot dog, current project: Waveshare ESP32-S3-Touch-AMOLED-1.43, 5 servos, ESP-IDF | `robotdog/CLAUDE.md`, `robotdog/docs/ROBOTDOG_PLAN.md` |
| `microduck/` | v3 | Walking Microduck biped, analysis only | `microduck/CLAUDE.md`, `microduck/docs/MICRODUCK_PLAN.md` |

- Keep the projects apart: a change to one shouldn't touch the other's files. Don't copy code between them unless the owner asks (the wheelbot firmware is GPLv3 because of RoboEyes; the dog firmware is MIT; the Microduck reference code is GPL-3.0).
- `robotdog/firmware/` is a **git submodule** (its own repo, `IvannnHuang/tabledog`). Commit there first, then commit the updated submodule pointer here.
- Third-party reference clones go in a git-ignored `reference/` folder inside their project. Read them, don't edit or commit them.
- Keep each plan up to date when hardware, pins or progress change.
- **Secrets:** WiFi passwords, MQTT logins and API keys go only in git-ignored files (`secrets.h`, server config). Never commit them or print them in chat. AI keys live only on a server, never in firmware or a web page.
- The owner uploads to hardware themselves. Don't send motor or servo commands to a connected board without asking.

## Hardware rules shared by all robots (learned the hard way)
- Flash the firmware before connecting motor or servo power (floating pins left a motor stalled until it overheated).
- No non-rechargeable lithium cells (ER14250) and no charger on primary cells. Use a protected LiPo that can supply the load's peak current.
- Motor or servo current never flows through the microcontroller board; share ground only.
- The ESP32 only does 2.4 GHz WiFi.
- OLED/AMOLED burn-in: keep the eyes moving, dim the sleep screen, only a small moving clock at night. Anything that stays on screen shifts a few pixels now and then.

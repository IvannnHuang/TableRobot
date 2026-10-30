# TableRobot

Small desk companion robots with animated faces, one for each of us. The repo holds one project per robot version:

| Version | Project | Folder | Status |
|---|---|---|---|
| v1 | **Wheelbot** (RoboFace): two-wheeled robot, XIAO ESP32-C3, 0.96" OLED face | [`wheelbot/`](wheelbot/) | Finished |
| v2 | **Robot dog**: four legs + a turning neck, ESP32-S3 with a 1.43" round AMOLED touch face, IMU, mic and speaker; offline voice commands, web and remote control, two dogs that keep each other company over the internet, AI chat last | [`robotdog/`](robotdog/) | Planning, board bought |
| v3 | **Microduck**: walking biped on an ESP32-S3 with a touch screen + camera head | [`microduck/`](microduck/) | Analysis only |

Each folder has its own README, plan (`docs/`) and notes for Claude Code (`CLAUDE.md`).

## Getting the code
The robot dog firmware is a separate git repo ([IvannnHuang/tabledog](https://github.com/IvannnHuang/tabledog)), included here as a submodule in `robotdog/firmware/`. Clone with:
```
git clone --recurse-submodules <this repo>
```
or, in an existing clone, run `git submodule update --init`.

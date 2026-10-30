# Microduck: notes for Claude Code

TableRobot **v3**: a walking Microduck biped (14 Dynamixel XL330 servos) on an ESP32-S3 instead of the Pi, with a touch screen + camera head instead of the duck head. **The analysis, servo prices, risks and pending decisions are in `docs/MICRODUCK_PLAN.md`; read it first.** Separate from `../wheelbot/` (v1) and `../robotdog/` (v2).

## Current state
- **Analysis only (2026-10-30).** The owner wants analysis before changes: don't write firmware, order parts or build for this until they decide (plan section 8). It comes after the robot dog.

## Reference code
- `reference/Microduck-build-tutorial/` (when cloned) is a third-party **GPL-3.0** clone with its own `.git`, git-ignored. Read it as reference, don't edit it, and don't `git add` it.
- The clone's training robot model was deleted (commit `34656ce`); get it from upstream `pollen-robotics/microduck`.

## Key facts
- The policy is a 51→512→256→128→14 MLP at 50 Hz, and the neck/head joints are part of it, so a new head means retraining (mjlab + NVIDIA Warp, so an NVIDIA GPU).
- Hobby servos can't run the policy (no position/velocity feedback). An S3 can run it (int8 weights in internal RAM); a C3 can't (no FPU).
- Keep the BNO085 in the torso as the policy's IMU; a head-board IMU moves with the head.
- Recommended split: head board (face, camera, network, AI) + body board (Dynamixel bus, IMU, policy only).

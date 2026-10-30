# Microduck: walking robot (TableRobot v3)

A walking biped based on Pollen Robotics' **Microduck** (14 servos, a small neural-network walking policy), run by an ESP32-S3 instead of a Raspberry Pi, with a touch screen + camera head instead of the duck head.

- **v3 of TableRobot**, after the robot dog (v2, `../robotdog/`). v1 is the finished wheelbot (`../wheelbot/`).
- **Status (2026-10-30): analysis only.** Nothing ordered, built or written. Don't change firmware, order parts or build for it until the owner decides the questions in section 8, and not before the robot dog is done.

Last updated: 2026-10-30

---

## Contents
1. [The idea](#1-the-idea)
2. [What Microduck is](#2-what-microduck-is-from-the-code)
3. [Can an ESP32 replace the Pi?](#3-can-an-esp32-replace-the-pi)
4. [Blockers and risks](#4-blockers-and-risks-biggest-first)
5. [Servo options with feedback](#5-servo-options-with-feedback-prices-checked-2026-10-30-they-change-often)
6. [Recommended architecture](#6-recommended-architecture)
7. [Suggested order](#7-suggested-order)
8. [Pending decisions](#8-pending-decisions-the-owners-call)
9. [Reference code](#9-reference-code)

---

## 1. The idea
- Build a walking Microduck biped (14 servos) as the next step after the robot dog.
- Run it on an **ESP32** instead of the Raspberry Pi.
- Replace the duck head with a **touch screen + camera** head. This brings back the camera features the robot dog leaves out.
- Reuse the robot dog's work for the head: face, touch, voice, MQTT link to the partner's robot, AI.

## 2. What Microduck is (from the code)
| Item | Value |
|---|---|
| Controller | Raspberry Pi Zero 2 W (Python, onnxruntime) → USB → ROBOTIS OpenRB-150 → Dynamixel bus at 1 Mbps |
| Servos | 14× Dynamixel XL330-M288-T: 10 leg + 4 neck/head (`neck_pitch`, `head_pitch`, `head_yaw`, `head_roll`). Optional ID 15 = mouth |
| IMU | BNO085 over I2C, in the torso |
| Power | 6 V rechargeable pack, 5 V regulator for the Pi. Overcurrent cutoff at 15 A total (`constants.py`) |
| Control loop | **50 Hz** (`scheduler.py`): read the 14 servo positions + velocities and the IMU → policy → write 14 target angles |
| Policy `walk.onnx` | MLP **51 → 512 → 256 → 128 → 14**, ELU, **192,756 weights** (~770 KB as float32). No vision |
| Policy input (51) | gyro 3, projected gravity 3, joint positions 14, joint velocities 14, last action 14, command vx/vy/vθ 3 |
| Servo gains | `KP_RL = 125` while walking (deliberately soft), `KP_DEFAULT = 400` otherwise |

- **The head and neck joints are part of the policy**, both as inputs and as outputs. The policy moves the head while walking, partly for balance.

## 3. Can an ESP32 replace the Pi?
- **ESP32-C3:** no. It has no FPU.
- **ESP32-S3:** yes.
  - A policy step is ~193k multiply-adds.
  - Quantise the weights to **int8 (~190 KB, fits in internal RAM)** or use Espressif ESP-DL. Streaming 770 KB of float32 weights from flash or PSRAM 50×/s is too slow.
  - Check the outputs against onnxruntime on logged observations before using it on the robot.
- **Porting:**
  - Rewrite ~1,200 lines of Python (`scheduler.py`, `observer.py`, `moves/walk.py`, `imu_reader.py`, `constants.py`) as C++.
  - Run Dynamixel2Arduino on the ESP32, with a half-duplex buffer chip on one UART, or keep the OpenRB-150 as a bridge.
- **IMU:** keep the BNO085 in the torso, which is what the policy was trained on. Don't use the head board's QMI8658: it moves with the head.
- **Control:** the existing phone joystick (`joy`) can feed the vx/vθ command in place of the gamepad.

## 4. Blockers and risks (biggest first)
1. **A new head means retraining.**
   - The screen + camera head has a different mass and centre of gravity, and the policy controls the neck.
   - Put the new head mass in the simulation model, then retrain.
2. **This clone can't retrain.**
   - The tutorial author deleted `mjlab_microduck/src/mjlab_microduck/robot/` (commit `34656ce`), so `MICRODUCK_ROBOT_CFG` is missing.
   - The MJCF in `microduck/src/model/mjcf/` is an older model (arms, a single head joint) that doesn't match the 14-DoF policy.
   - Get the robot model from upstream `pollen-robotics/microduck`.
   - Training needs mjlab 1.3.0 + NVIDIA Warp, so an NVIDIA GPU (local or cloud).
3. **Hobby servos (SG90/MG90S) can't be used.**
   - They report no position or velocity, but 28 of the 51 policy inputs are joint position and velocity.
   - Their stiffness is fixed and they have a dead zone and backlash, while the policy was trained on soft XL330s with a matching motor model (BAM).
   - They are too weak (SG90 ~0.18 N·m against the XL330's 0.52 N·m), and they can't report current, so the overcurrent cutoff can't work.
   - Hobby-servo robot dogs work because they play pre-programmed gaits on four statically stable legs. A biped needs the feedback loop.
4. **Camera + display + 50 Hz loop on one S3.**
   - The AMOLED-1.43 board has no camera port and 11 usable header pins (15 without the SD card; see the robot dog plan, 2.3), and a parallel (DVP) camera needs ~13–16 on top of everything else.
   - Camera and display DMA would also cause timing jitter in the walking loop.
   - On-device vision on an S3 is limited (face detection or tracking at a few fps). Anything more means sending JPEGs to the cloud AI.
5. **Power is a different class.** The pack is 6 V with peaks of several amps. The 1S LiPo setups of the wheelbot and the robot dog don't carry over, and the ESP32 needs its own regulator. The shared hardware rules in the root `CLAUDE.md` still apply.
6. **A biped on a desk** falls off edges. The tutorial itself says not to test near a table edge.

## 5. Servo options with feedback (prices checked 2026-10-30; they change often)
| Servo | Price found | Torque | Size / weight | Verdict |
|---|---|---|---|---|
| **Dynamixel XL330-M288-T** (stock) | **$23.90** from ROBOTIS (en.robotis.com), $27.49 on robotis.us; $40–60 from third-party sellers | 0.52 N·m @ 5 V | 20 × 34 × 26 mm, 18 g, 4096 steps/turn | **Best value at Microduck size.** No redesign needed |
| Feetech STS3032 | ~$29–49 retail (RCmall sells it on Amazon) | 4.5 kg·cm @ 6 V | 32 × 12 × 27.5 mm, ~18–20 g, 12-bit magnetic encoder | Closest drop-in, but not cheaper. Needs new mounts + retraining |
| Feetech STS3215 / Waveshare ST3215 | ~$17–22 (Amazon 30 kg 2-pack ≈ $44) | 19.5–30 kg·cm | Much bigger (~45 × 25 × 35 mm, ~55 g, from memory) | Too big for Microduck. The best value per unit of torque for a bigger robot (Pollen's Open Duck Mini walks on these) |
| Feetech SCS0009 | Cheap micro servo (Amazon price not confirmed) | 2.3 kg·cm | Micro, ~10 g | Too weak for the legs. OK for the 4 neck/head joints. Potentiometer sensor, and listings disagree on specs |
| Hiwonder HX-06L | $15.99 (Hiwonder store) | 6 kg·cm | Small | Probably no. 115200 baud and one servo read at a time ≈ 40 ms for 14 servos, more than the 20 ms tick. No velocity feedback |

- **Cost per robot with XL330s:** 14 × $23.90 ≈ **$335**, plus OpenRB-150 (or a buffer chip), BNO085, battery, and a U2D2 for setting servo IDs (one can be shared). About **$850+ for the pair**, against about $80–95 per robot dog.
- **Cheaper hybrid:** XL330s on the 10 leg joints (~$240) and SCS0009s on the 4 neck/head joints. This needs retraining, but the new head needs that anyway.

## 6. Recommended architecture
```
 Head board: ESP32-S3 + touch screen + camera   ──UART through the neck (4 wires)──►  Body board: ESP32-S3
   eyes/face, touch, camera, mic/speaker,             or ESP-NOW                         Dynamixel bus, BNO085,
   WiFi, MQTT, cloud AI (as on the dog)                                                 int8 policy at 50 Hz
   sends "walk vx/vθ, look at x/y"                                                      (only real-time work)
```
- Most of the robot dog's firmware (face, touch, MQTT, voice, AI) can move to the head board.
- The body board only does the time-critical walking loop.
- Pick a head board with a real camera connector, for example a Waveshare S3 touch LCD with a camera port (check before buying).

## 7. Suggested order
1. Get the full upstream `pollen-robotics/microduck` repo with the robot model, and confirm training runs on an available GPU.
2. Build **one** robot to the tutorial spec (Pi + stock head) and confirm it walks with the stock `walk.onnx`.
3. Port the policy to the S3 body board. Compare its outputs with the Pi's on the same observations.
4. Design the new head, put its mass in the simulation model, retrain, then swap the head.
5. Only then build the second robot.

## 8. Pending decisions (the owner's call)
- [ ] Is ~$850+ in servos for the pair acceptable?
- [ ] Servo choice: XL330 for all 14, or the hybrid (XL330 legs + SCS0009 neck)?
- [ ] Head board with a camera (model to choose). Reuse the robot dog's AMOLED board only if the camera can live on another board
- [ ] Where to train: local NVIDIA GPU or cloud
- [ ] One prototype first with the stock Pi + head (section 7, step 2)?

---

## 9. Reference code
- `Microduck-build-tutorial/`: a clone of `github.com/AI-FanGe/Microduck-build-tutorial` (GPL-3.0), based on `pollen-robotics/microduck` and `Rhoban/microban`. Third-party code: read it, don't edit it, don't commit it.
  - `microduck/`: deploy code that runs on the Pi
  - `mjlab_microduck/`: training code (MuJoCo/mjlab)
- Keep reference clones in `microduck/reference/` (git-ignored). The clone used for this analysis was made in another copy of the repo; clone it again there when needed.
- Upstream for the robot model and training: `pollen-robotics/microduck`.

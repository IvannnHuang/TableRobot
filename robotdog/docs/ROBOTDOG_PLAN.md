# Robot dog: long-distance companion pet robots

Two small four-legged robot dogs, one for me and one for my partner. Each dog has a round touch screen face, four servo legs and a servo neck, a microphone and a speaker. It obeys simple voice commands offline, can be controlled from a phone at home or from anywhere, and keeps the other dog company over the internet. AI conversation comes last.

- Follow-up to the finished wheeled robot (`../wheelbot/`), which stays as it is.
- Idea and firmware base: **tabledog** (caiyu-dog), a four-servo desk dog built on the open-source XiaoZhi voice firmware. Our fork is in `firmware/`.

Last updated: 2026-10-29

---

## Current progress (2026-10-29)
- **Board:** Waveshare **ESP32-S3-Touch-AMOLED-1.43** bought. Checked against the official schematic (section 2.2).
- **Firmware:** tabledog forked to [IvannnHuang/tabledog](https://github.com/IvannnHuang/tabledog) and added here as the submodule `firmware/`. Still the original ESP32-C3 board only; nothing ported yet.
- **Tools:** ESP-IDF v6.0.2 is not installed yet.
- **Next:** roadmap step D0, bring-up (section 6).

### Folder layout
```
robotdog/
├── CLAUDE.md               context for Claude Code sessions on the dog
├── README.md               short overview
├── docs/ROBOTDOG_PLAN.md   this plan
└── firmware/               git submodule → IvannnHuang/tabledog (ESP-IDF, C++)
    ├── AGENTS.md           the firmware's own rules (board chain, state machine, style)
    ├── main/boards/caiyu-c3/   original tabledog board: legs, eyes, web console
    └── main/boards/<our board>/  to be added in D0
```

---

## Contents
1. [Goals](#1-goals)
2. [Hardware](#2-hardware)
3. [Firmware](#3-firmware)
4. [Control and networking](#4-control-and-networking)
5. [AI conversation (last step)](#5-ai-conversation-last-step)
6. [Roadmap](#6-roadmap)
7. [Safety rules](#7-safety-rules)
8. [Budget](#8-budget)
9. [Open questions](#9-open-questions)

---

## 1. Goals
- **A cute small dog:** stands, sits, lies down, walks, turns, waves, stretches, dances; the neck looks around, nods and tilts.
- **An expressive colour face** on the round AMOLED: emotions, blinking, sleepy eyes, burn-in safe.
- **Reacts to touch and motion:** petting, poking, being picked up, shaken, tipped over.
- **Offline voice:** a wake word and simple spoken commands with no internet.
- **Web control** from a phone on the same WiFi.
- **Remote control from any network,** and **two dogs that interact over the internet:** hugs, mood sync, copying each other's moves, dancing together, voice messages.
- **AI conversation** through a server, added last.

---

## 2. Hardware

### 2.1 Parts per dog
| Part | Choice | Status | Notes |
|---|---|---|---|
| Main board | Waveshare ESP32-S3-Touch-AMOLED-1.43 | **Bought** | Face, touch, IMU, RTC, battery charger built in (2.2) |
| Leg servos | 4× MG90S (or SG90) | to confirm | 180° servos. MG90S (metal gears) is safer: the round board is heavier than tabledog's OLED |
| Neck servo | 1× MG90S | to confirm | Carries the head (board + screen) |
| Microphone | INMP441 (I2S) | to confirm | The board has no mic |
| Amplifier + speaker | MAX98357A + 4–8 Ω, 2–3 W speaker | to confirm | The board has no speaker. Power the amp from the battery side, not the board's 3.3 V |
| Battery | 1S LiPo with protection, **≈2000 mAh or more, ≥3 A continuous** | to confirm | See 2.6: the board charges at about 2 A, and the servos draw 2–3 A peaks |
| Servo power | 5 V boost converter, **≥3 A** | to confirm | Fed from the battery behind the main switch |
| Switch | slide switch on battery + | to confirm | The board has no on/off switch |
| Capacitor | 470–1000 µF, ≥10 V, next to the servos | to confirm | Smooths servo current peaks |
| Header adapter | 1.27 mm → 2.54 mm, or fine wire | to confirm | The board's headers are 1.27 mm pitch |
| Separate charger board | — | **not needed** | The board charges the battery itself |

Camera features are dropped on purpose.

### 2.2 The board: ESP32-S3-Touch-AMOLED-1.43
Checked against Waveshare's wiki, product page and schematic (`ESP32-S3-Touch-AMOLED-1.43-Schematic.pdf`) on 2026-10-29.

| Item | Detail |
|---|---|
| Chip | ESP32-S3R8: 240 MHz dual core, 512 KB SRAM, **8 MB PSRAM**, WiFi 2.4 GHz + BLE 5 |
| Flash | **16 MB** (W25Q128) |
| Display | 1.43" round AMOLED, 466×466, QSPI. Datasheet says SH8601; some batches ship with a CO5300 (xiaozhi has drivers for both) |
| Touch | FT3168, I2C |
| IMU | QMI8658 6-axis, I2C, interrupt on GPIO 8 |
| RTC | PCF85063, I2C, interrupt on GPIO 15; SH1.0 connector for a backup coin cell |
| Battery charger | **ETA6098**, charge current set by R23 = 82 kΩ → **about 2 A** (from the schematic's ISET table) |
| Power path | USB 5 V → Schottky → VCC; battery → AO3401 P-MOSFET → VCC, switched off while USB is present. **USB can't back-feed the battery** |
| 3.3 V regulator | MP1605 buck, max 2 A |
| Battery plug | MX1.25 / PH1.25, 2-pin. Check the polarity |
| Battery reading | GPIO 4 reads **VCC / 3** (200 k / 100 k divider): battery voltage on battery, about 5 V on USB |
| SD card | TF slot on GPIO 38–41 (SPI) |
| Buttons | BOOT (GPIO 0), RESET |
| Audio | **none on board** |
| Size | not stated in text; Waveshare has a 3D model download (`ESP32-S3-Touch-AMOLED-1.43-3DFile.zip`) for the CAD |

The **1.43C** variant has a built-in mic and speaker but **no IMU, RTC or SD slot**, so the plain 1.43 is the right board for this project.

**GPIOs used on the board:**
| GPIO | Use |
|---|---|
| 0 | BOOT button |
| 4 | battery reading (VCC/3) |
| 8 | IMU interrupt |
| 9–14 | display QSPI (CS, CLK, D0–D3) |
| 15 | RTC interrupt |
| 19 / 20 | USB D− / D+ (native USB: flashing and logs) |
| 21 | display reset |
| 38–41 | SD card (CS, MOSI, MISO, SCLK) |
| 42 | display enable |
| 43 / 44 | UART TXD / RXD (also on the header and a SH1.0 connector) |
| 47 / 48 | I2C SDA / SCL: touch, IMU, RTC (also on the header and a SH1.0 connector) |

### 2.3 Expansion headers (from the schematic)
Two 2×7 headers, **1.27 mm pitch**:
```
J1                                   J3
 1 U_P (USB D+)   2 VBAT              1 3V3        2 IO18
 3 U_N (USB D−)   4 USB_5V            3 SCL(IO48)  4 IO38 (SD)
 5 IO6            6 GND               5 SDA(IO47)  6 IO39 (SD)
 7 IO5            8 IO16              7 GND        8 IO40 (SD)
 9 IO3           10 IO17              9 RXD(IO44) 10 IO41 (SD)
11 IO2           12 IO7              11 TXD(IO43) 12 IO46 (strapping)
13 IO1           14 IO0 (BOOT)       13 3V3       14 IO45 (strapping)
```
- **Free:** 1, 2, 3, 5, 6, 7, 16, 17, 18.
- **Also usable:** 43 / 44 (logs go over native USB instead), and 38–41 if the SD card isn't used.
- **Avoid:** 0 (BOOT), 45 / 46 (checked by the chip at start-up).
- **Don't** take servo current from the VBAT or USB_5V header pins: 1.27 mm pins aren't made for amps.

### 2.4 Pin map (planned)
| Part | Signal | GPIO | Header pin |
|---|---|---|---|
| Servo, left front leg | PWM | 1 | J1-13 |
| Servo, right front leg | PWM | 2 | J1-11 |
| Servo, left back leg | PWM | 3 | J1-9 |
| Servo, right back leg | PWM | 5 | J1-7 |
| Servo, neck | PWM | 6 | J1-5 |
| INMP441 + MAX98357 | BCLK (shared) | 16 | J1-8 |
| INMP441 + MAX98357 | WS / LRC (shared) | 17 | J1-10 |
| INMP441 | SD (mic data) | 7 | J1-12 |
| MAX98357 | DIN (speaker data) | 18 | J3-2 |
| Spare | — | 43, 44 (+38–41 without SD) | J3 |

- Mic and amp share the clock lines ("duplex"), so both run at **16 kHz**. If that sounds poor, move to separate clocks using the spare pins 43 / 44.
- INMP441: VDD 3.3 V, L/R → GND. MAX98357: VIN from the battery side, GAIN and SD pins per its datasheet.
- Servo signals are 3.3 V; MG90S/SG90 accept that.
- A PCA9685 servo driver isn't needed: there are enough pins.

### 2.5 Power
```
1S LiPo (protected, ≥2000 mAh, ≥3 A)
   + ── slide switch ──┬── board battery plug (MX1.25)   → board + display + mic (3.3 V)
                       ├── MAX98357 VIN
                       └── 5 V boost (≥3 A) ──┬── 5 servos (V+)
                                              └── 470–1000 µF capacitor
   − ───────────────────── board GND ── boost GND ── servo GND ── amp GND   (shared ground)
```
- The board charges the battery over its own USB-C (about 2 A). Charge with the **servos switched off**, or at least idle.
- No diode is needed between USB and battery: the board's power path already prevents back-feeding.
- Four or five servos moving together can pull 2–3 A from the battery through the boost converter. A weak cell sags and resets the board.
- **About the 2 A charge current:** fine for a ≈2000 mAh cell rated for 1C charging; too fast for a small cell (an 1100 mAh cell would charge at ≈1.8C). Changing R23 (tiny surface-mount part) is the alternative: 180 kΩ ≈ 1 A, 360 kΩ ≈ 0.5 A.

### 2.6 Mechanical (CAD)
- To design: body, four legs, neck joint and head. The head holds the round board; the speaker and mic need openings.
- Get the board's exact size from Waveshare's 3D model before designing the head.
- Keep the battery low and central so the dog doesn't tip forward; the head on the neck servo moves the centre of gravity.
- Put CAD files in `robotdog/cad/` (create it with the first file).

---

## 3. Firmware

### 3.1 Base: tabledog (XiaoZhi fork)
- **ESP-IDF v6.0.2, C++**, not Arduino. Tabledog is the XiaoZhi voice firmware trimmed to one board, `caiyu-c3` (ESP32-C3, SH1106 OLED, 4 servos, WS2812 strip, touch pad).
- The firmware's own rules are in `firmware/AGENTS.md`. The most important one: a board is selected through a chain, `config.json` → `scripts/build.py` → `main/Kconfig.projbuild` → `main/CMakeLists.txt` → board source and `config.h`. Adding a board means updating every link.

| From tabledog | Plan |
|---|---|
| `caiyu_actionments.*`: leg moves, speed, trim saved in NVS, mood moves | **Reuse**, add the neck servo and neck moves |
| `self.dog.action` and other MCP tools | **Reuse** (AI and remote control call the same tools); translate descriptions to English |
| Web console (`web/`): remote, emotions, settings, trim sliders | **Reuse**, translate, add neck and face controls |
| Emotion list (21 expressions) | **Reuse** the list |
| OLED eye renderer (128×64, mono) | **Replace** with colour eyes in LVGL at 466×466 |
| WS2812 strip + touch pad on one pin | **Drop**; the AMOLED touch replaces the touch pad |
| Voice chat to the XiaoZhi server | **Keep**, but add an offline mode (3.3) |

### 3.2 Our board folder
- New folder `main/boards/robotdog-s3-amoled143/` (name to confirm) with its own `config.h`, `config.json` and one `DECLARE_BOARD`.
- Start the display and touch code from upstream XiaoZhi boards: `waveshare/esp32-s3-touch-amoled-1.43c` (SH8601 init sequence) and the other `waveshare/esp32-s3-touch-amoled-*` boards (CO5300, FT3168 touch). Those boards are not in our trimmed fork; copy what's needed from [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32).
- Audio: `NoAudioCodecDuplex` (I2S, no codec chip) on the pins in 2.4.
- New: QMI8658 IMU, PCF85063 RTC, battery reading, neck servo.

### 3.3 Offline mode and offline voice commands
- **Wake word:** ESP-SR WakeNet, already in XiaoZhi, runs on the chip.
- **Commands:** ESP-SR **MultiNet** is already wired in as a "custom wake word" (`main/audio/engines/afe_audio_engine.cc`). Register phrases like "sit down", "dance", "look left", "go to sleep" (English, up to about 200), and when one is heard **run the action instead of opening a chat**.
- **No server needed:** at start-up XiaoZhi checks its server for activation and updates, retrying with doubling delays (`Application::CheckNewVersion`). Add an offline mode that skips this when no server is configured.
- Replies offline: sound effects and short pre-recorded clips (SD card or flash). Espressif's on-chip text-to-speech is Chinese only.
- With no hardware echo reference (separate mic and amp), conversation is **half-duplex**: the mic is muted while the dog speaks.

### 3.4 Build
- Install **ESP-IDF v6.0.2** (not installed yet).
- `python scripts/build.py <board>` from `firmware/`, then `idf.py -p <PORT> flash monitor`.
- The board has native USB: flashing and logs go over USB-C.

---

## 4. Control and networking

### 4.1 At home: web console
Tabledog's console is served by the dog itself: `http://<dog-ip>/`. Tabs for remote (legs), emotions, settings (servo trim). Add neck and face controls.

### 4.2 From any network: cloud MQTT relay
A phone can't connect straight into a home network, so both sides connect **out** to a relay:
```
 My phone (any network) ──┐                                  ┌── Partner's phone
                          ├──► MQTT broker (cloud, TLS) ◄────┤
 My dog (home WiFi) ──────┘    HiveMQ Cloud / EMQX free tier └── Partner's dog (their WiFi)
```
- No port forwarding or router setup at either home.
- Latency is usually a fraction of a second: fine for buttons, the joystick, hugs, moods and moves. Voice messages go as an uploaded file plus a link.
- ESP-IDF has an MQTT client built in (`esp-mqtt`).
- **Phone app:** a web page added to the home screen, hosted free (GitHub Pages / Netlify), using MQTT over secure WebSockets (mqtt.js).
- **Setup:** the dog creates a setup hotspot; a phone page sets the WiFi, dog name, pair ID and MQTT login. The partner never needs a compiler.

**Topics:**
```
pair/<pairId>/<dogId>/status     retained: online/offline (last will), battery, mood
pair/<pairId>/<dogId>/event      things that happened: petted, picked_up, shaken, heard_name
pair/<pairId>/<dogId>/cmd        commands TO this dog: from its owner's app or the partner's dog
pair/<pairId>/<dogId>/media      links to voice messages, sounds, drawings
```

**Example messages:**
```json
{ "type": "action", "name": "sit" }
{ "type": "neck",   "angle": 30 }
{ "type": "hug",    "from": "dogA" }
{ "type": "mood",   "value": "happy" }
{ "type": "face",   "name": "heart_eyes", "ms": 3000 }
{ "type": "sound",  "name": "bark", "volume": 60 }
{ "type": "voice",  "url": "https://.../msg123.opus", "from": "me" }
{ "type": "dance",  "name": "swing", "startAt": 1761600000 }
```

**Security:**
- A separate MQTT login per dog and per phone, each limited to `pair/<pairId>/#`.
- TLS always (port 8883 on the dogs, secure WebSocket in the app).
- No admin logins or AI keys in the firmware or the web app.
- The microphone only sends audio after the wake word or while the screen is held, with an on-screen icon.

### 4.3 Dog-to-dog interactions
- **Hug:** pet your dog's screen for 2 s → the partner's dog shows hearts and wags.
- **Mood sync:** a small icon shows the other dog's mood.
- **Copy me:** move one dog (or press buttons), and the other copies the moves.
- **Dance together:** synchronised dance from a shared start time.
- **"I'm home" / "Good night":** the other dog greets its owner or lies down to sleep.
- **Voice messages:** hold to record, the other dog plays it.
- **Meeting in person:** ESP-NOW detects the other dog nearby, and they greet each other.
- **Later, fun extras:** funny faces, a sound board, music the dog dances to, drawing on the partner's screen, little games.

---

## 5. AI conversation (last step)

### 5.1 How it works
The dog is the voice terminal; a **server** does the thinking. A full chat model can't run on the ESP32-S3 (8 MB PSRAM vs about 0.5–1 GB for even a 1B-parameter model).
```
 Dog (ESP32-S3)                         AI server
 0. on boot: "where's my server?" ────► config endpoint: WebSocket URL + token
 1. wake word (on the chip)
 2. streams mic audio (Opus) ─────────► 3. speech-to-text
                                        4. LLM + personality + memory + the dog's tool list
 5. runs the tool (legs, neck, face) ◄─ 6. tool call, e.g. self.dog.action("wave")
                         result ──────► 7. LLM writes the reply
 9. plays audio, face shows emotion ◄── 8. text-to-speech → Opus audio + emotion tag
```
- The dog only needs its server address (`OTA_URL`) and a device token. All API keys stay on the server.
- The tools are the same ones the web console and MQTT use, so AI control comes for free.

### 5.2 Server options
| | A. Public xiaozhi.me | B. Self-hosted xiaozhi-esp32-server | C. Own server |
|---|---|---|---|
| Setup | none: the dog shows a 6-digit code | Docker or Python on a PC, YAML config | build everything |
| Uses Claude | no (Chinese models) | **yes** | yes |
| Data | their servers (China) | my machine | my machine |
| Personality / memory / partner events | basic | full control | full control |

- **B:** [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server). Supports MCP device tools, Edge TTS, OpenAI/Groq/local speech-to-text, and any OpenAI-compatible LLM. Its authors say it has no security review: don't expose it to the internet unprotected.
- **Claude in B:** through Claude's OpenAI-compatible endpoint (base URL `https://api.anthropic.com/v1/`, a Claude API key, a Claude model name). Remove `temperature` / `top_p` from the server's LLM settings (current Claude models reject most values). The endpoint has no prompt caching and is meant for testing; a small native Claude provider for the server can come later.
- **Model choice** (prices as of October 2026; check the current pricing page): Claude Opus 5.5 (most capable, about 1.4¢ per voice turn of ~3K input / 100 output tokens), Claude Sonnet 5.5 (middle), Claude Haiku 5.5 (fastest, about 0.04¢ per turn).
- **No-cloud alternative:** the same server with local speech-to-text, a local LLM through Ollama and local text-to-speech, on a PC with a GPU (about 1–3 s replies). A Raspberry Pi 5 is too slow for comfortable voice chat.
- **Reaching the server from the partner's home:** a public address with TLS (`wss://`), via a VPS or a tunnel (for example Cloudflare Tunnel).

### 5.3 Plan
1. Optional quick test on A to prove the voice chain on the board.
2. Then B with Claude: personality prompt per dog (name, owner names, partner dog's name), short memory, English voice.
3. **Event layer:** the dog sends a short summary of recent touch/IMU/partner events with each request ("was petted 3 times, then picked up"), so replies match how it's being treated. Reflexes stay on the dog and never wait for the server.
4. Partner events as context ("your partner sent you a hug an hour ago").

---

## 6. Roadmap
Each step ends with something that works. Don't start a step until the one before works reliably.

### D0: board bring-up
- [ ] Install ESP-IDF v6.0.2; build the existing `caiyu-c3` board to prove the toolchain
- [ ] Flash Waveshare's demo once: display, touch, IMU, RTC and battery reading work
- [ ] New board folder (3.2): display (SH8601 or CO5300), FT3168 touch, LVGL running
- [ ] QMI8658, PCF85063 and battery reading in our board code
- [ ] Offline mode: boots with no server configured

**Done when:** our own firmware shows a test face, reacts to touch, prints IMU data, and boots without a server.

### D1: body (legs + neck)
- [ ] Power system (2.5): switch, boost, capacitor, battery; test with **no servos** first
- [ ] Flash, then connect servo power; centre all servos before fitting the horns
- [ ] Port `caiyu_actionments` to the 5 pins; add the neck (look left/right, nod, tilt, follow the eyes)
- [ ] Servo trim from the web console, saved in NVS
- [ ] Print the body; tune the gait for the heavier head

**Done when:** the dog stands, sits, walks, turns and looks around on battery for 30 minutes without resets.

### D2: face and reactions
- [ ] Colour eyes in LVGL at 466×466, the 21 emotions, blinking, sleepy eyes
- [ ] Burn-in protection: eyes always moving, dim sleep screen, small moving clock at night
- [ ] Touch: stroke = happy, tap = blink, long press = annoyed
- [ ] IMU: picked up = surprised and legs freeze; shaken = dizzy; tipped over = "help!"
- [ ] Sleep screen with clock (RTC) and battery level

**Done when:** the face and body react sensibly to petting, poking, being picked up and shaking.

### D3: offline voice and sound
- [ ] Wire the INMP441 and MAX98357 (2.4); test playback and recording
- [ ] Wake word
- [ ] 20–50 MultiNet command phrases mapped to actions
- [ ] Sound effects: bark, whine, yawn, happy chirps; volume and mute

**Done when:** "wake word, sit down" works with no internet.

### D4: local web control
- [ ] Tabledog's console on the dog, translated to English
- [ ] Neck, face and sound controls
- [ ] QR code on screen with the console address

**Done when:** everything the dog can do is reachable from a phone on the home WiFi.

### D5: remote control from anywhere
- [ ] MQTT broker account; per-device logins
- [ ] MQTT client on the dog: status with last will, commands, events
- [ ] Phone web app (home-screen page): drive, actions, face, status
- [ ] Setup hotspot page for WiFi, name, pair ID and MQTT login

**Done when:** the dog can be driven from a phone on mobile data, with commands arriving in under a second.

### D6: second dog and dog-to-dog
- [ ] Build the second dog
- [ ] Pairing; hugs, mood sync, copy-me, dance together (4.3)
- [ ] Voice messages
- [ ] ESP-NOW greeting when both dogs are nearby

**Done when:** a hug sent from either dog or phone reaches the other dog in under 2 seconds, across different networks.

### D7: AI conversation
- [ ] Server (5.2) with Claude or a local model
- [ ] Personality, memory, event layer, partner events (5.3)

**Done when:** a 5-turn conversation in which the dog's moves and face match what it says, with replies in under 5 s.

---

## 7. Safety rules
Shared rules are in the repo's root `CLAUDE.md`. For the dog:

| Rule | Why |
|---|---|
| Flash the firmware before connecting servo power; switch servos off while uploading | Floating pins make servos twitch or push against their end stops (same lesson as the wheelbot's stalled motor) |
| Servo current goes battery → boost → servos, never through the board or its header pins | Board traces and 1.27 mm pins can't carry 2–3 A; peaks reset the S3 |
| Battery ≥3 A continuous, ≈2000 mAh+, protected | Servo peaks + the board's ≈2 A charging |
| Servos off (or idle) while charging | The charger and the servos share the battery |
| Check the battery plug polarity before connecting | JST/MX wire colours aren't standard |
| Test new servos one at a time at 5 V; never leave a servo stalled | Stalled servos overheat |
| Don't move servos on a connected dog without asking the owner | The legs may be under load or not fitted |
| AMOLED burn-in: eyes always moving, dim when asleep, small moving clock at night | AMOLED pixels age |

---

## 8. Budget (approximate, per dog)
| Item | Cost |
|---|---|
| ESP32-S3-Touch-AMOLED-1.43 | ~$36 (bought) |
| 5× MG90S servos | ~$15–25 |
| INMP441 + MAX98357 + speaker | ~$10 |
| 1S LiPo ≈2000 mAh, protected | ~$10–15 |
| 5 V 3 A boost, switch, capacitor, wires, 1.27 mm adapter | ~$10 |
| 3D printing | filament |
| **Total** | **about $80–95** |

Running costs: MQTT broker and web app hosting on free tiers; AI cents per conversation with Claude, or free with a local model.

---

## 9. Open questions
- [ ] Which parts are already bought (servos, battery, charger board)? The separate charger board isn't needed.
- [ ] Board size from the 3D model → head and body design.
- [ ] Display chip on this board: SH8601 or CO5300?
- [ ] SD card used (sounds, voice messages) or are GPIO 38–41 needed for something else?
- [ ] Board folder name in the firmware.
- [ ] MQTT broker (HiveMQ Cloud or EMQX) and the pair ID.
- [ ] Dog names, personalities and the wake word ("Hi ESP" or a custom one, which needs Espressif training).
- [ ] AI: server location (PC, VPS, Pi) and model (Claude Opus / Sonnet / Haiku 5.5, or local).

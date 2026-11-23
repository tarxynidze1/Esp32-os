# ESP32-OS repair report

The audit covered both files in the original repository at `d92fcd3` and the complete Arduino sketch, including its game assets, all 12 menu entries, storage, and hardware and networking paths. There were no submodules. Six GPT-6.2 Sol agents worked on core scheduling, Racer, graphics, other games, hardware, and integration. All six reviewed the integrated checkout. The final review uncovered an OLED edge pixel in Racer, an ADXL345 recovery case, and a Wi-Fi scan re-entry bug. Those fixes were followed by another target build and host test pass.

## Racer

| Severity | Function or path | Root cause and fix |
| --- | --- | --- |
| High | `loopRacer`, `RacerGame::advance` | Steering and obstacle movement ran on each sketch loop, so bus activity changed game speed. A 60 Hz fixed step uses elapsed microseconds and unsigned rollover arithmetic. The accumulator caps catch-up at 100 ms. Entering the game rebases its clock after the menu transition. |
| High | `RacerGame::reset`, `RacerGame::step` | Independent obstacle speeds allowed cars to bunch. Three lanes share one speed, with randomized gaps of 60–75 px. Speed rises from 86 px/s to a cap of 200 px/s. |
| High | `RacerGame::collides`, `RacerGame::step` | The original collision box skipped visible wheel contacts and could recycle cars before testing collisions. Four substeps compare the rounded body and wheel rows against overlapping sprite rows before awarding passes. |
| High | `RacerGame::step`, `loopRacer` | Scoring, crash, and restart could leave inconsistent state. Only a car that has fully passed the road awards one point. A crash freezes the scene and records the high score once. Short Select resets the player, road, obstacles, score, and timing. |
| Medium | `loopRacer`, `readADXL` | Unscaled tilt and stale reads made steering drift and tied controls to an optional sensor. A dead zone and bounded steering rate use fresh samples. Directional buttons override tilt and work independently. |
| Medium | `drawRacer`, `renderFrame` | Wall-clock road patterns jumped as difficulty changed and sprites could overwrite the score. Road markings move with simulated distance, cars remain visible through the last road pixel, and the score and crash overlay are drawn above the road. The OLED presenter skips unchanged bytes and attempts up to 60 refreshes per second in fullscreen. |

At maximum speed the minimum 60 px separation gives conservatively 28 px of longitudinal clearance after accounting for both 16 px cars. That is 140 ms to steer across an 18 px lane, which takes about 115 ms at full steering. Seeded tests drive the game through maximum difficulty and verify that the gap remains valid after recycling. Collision tests distinguish wheel contact from a clear pass and exercise crash freeze, restart, timer rollover, and dispatch stalls.

## Other significant repairs

Functions in this table are in `Esp32os/Esp32os.ino`.

| Severity | Area and functions | Root cause and fix |
| --- | --- | --- |
| High | Core `loop`, `renderFrame`, `updateActiveApp` | App animations and I/O competed in an unpaced loop. The scheduler polls input and services the active app independently of display pacing, yields to FreeRTOS, and bounds catch-up. DOOM enters landscape once and restores portrait before the exit transition. |
| High | I²C and startup `initHardware`, `presentDisplay` | Startup selected 800 kHz and could spin indefinitely on a display failure. Both devices now share a 400 kHz bus with checked startup and a bounded I²C timeout. Full display refresh is retried after partial-transfer errors. |
| High | ADXL345 `configureADXL`, `readADXL` | Unchecked I²C results could leave invalid or stale tilt data. The driver checks device ID and data-ready status and publishes only complete samples. It backs off after bus failures and reinitializes an idle sensor that reset between polls. |
| High | BLE `enterBleApp`, `loopBLE`, `exitBleApp` | HID deltas wrapped before being clamped and callbacks shared connection flags unsafely. Deltas saturate before encoding, connection events transfer atomically, and app exit releases mouse buttons. |
| Medium | Input `updateButtons`, `bp` | Debounce was based on the previous accepted edge. Each input now requires a stable raw interval and Select distinguishes a short release from a one-shot hold across timer rollover. |
| Medium | Graphics `presentDisplay`, `drawMenu`, game draws | The OLED retransferred unchanged scenes; selection text could blink during motion and some values clipped. The presenter compares shadow bytes and chooses smaller transfer windows. The menu inverts the actual moving highlight, and game HUD and previews stay in bounds. |
| High | DOOM `doomSprite`, `runDoomFrame` | The corpse sprite indexed a nonexistent frame. Valid frames, viewport clipping and per-column wall depth protect bitmap reads and render occlusion. |
| High | DOOM `updatePosition`, `fire`, `updateEntities`, `spawnWave` | Corner movement tested stale coordinates and attacks could pass through walls. Movement resolves axes in sequence, shots hit the nearest visible target, and bounded wave spawning accounts for walls and projectile slots. Enemies can route around blocking pillars. |
| Medium | DOOM `doomInit`, `loopDoom` | Restart retained weapon and movement state and fire input could be lost between frames. Restart resets the full run and the game queues a short Select release. |
| High | Snake `sSpawnFood`, `loopSnake` | Food could be misplaced with a stale occupancy calculation. It now chooses an empty cell from the full board and handles a completed board. Turns are queued per move, and entering a departing tail cell is legal when the snake is not growing. |
| Medium | Tetris `tMerge`, `loopTetris` | Valid wall rotations were rejected and merge assumed valid coordinates. Rotations test bounded horizontal offsets, merges check cell bounds, and simultaneous lines compact in order. |
| Medium | BLAST `bSelectNextShape`, `loopBlocks` | Shape selection could land on an unusable default or ambiguous state. Generation and placement share explicit states, select a fitting default, and clear simultaneous lines while preserving total score. |
| High | Calculator `cFormat`, `cCalc`, `cKey` | Large results could overflow the text buffer and scientific input could lose a pending right operand. Results use bounded formatting with numeric state kept separately. Domain errors and long display values have explicit handling. |
| High | Persistence `loadScoresAndNotes`, `serviceStorage` | EEPROM failures were ignored, loaded strings were trusted, and every score change committed synchronously. Startup validates contents while keeping the previous layout. Writes are coalesced and failed commits retried. |
| Medium | Notes `loopNotes`, `drawNotes` | Cancelled two-stage edits could apply a title early, deleted slots retained data, and long edit text spilled into the keyboard. Edits apply at confirmation, deleted slots are cleared, and title and body rendering stay inside the panel. |
| Medium | Math `mathGenerate`, `loopMath` | Difficulty arithmetic could overflow and the generator retried until random choices were distinct. Difficulty is bounded, answer generation is finite, level increments saturate, and restart resets timing. |
| Medium | ADXL demos `loopADXL`, `updateAdxlBalls`, `drawADXL` | Physics ran on every loop and used stale readings and repeated trigonometry. Demos have their own fixed update interval and reset state, balls remain in bounds, and cube projection reuses trigonometric values. |
| High | Wi-Fi and clock `loopWifi`, `serviceNetwork`, `handleClockSet` | A failed scan could spin, passwords were limited to short digit strings, and NTP was marked synchronized before a time update. Scan and connection failures have bounded states. The keyboard supports printable ASCII, and sync is shown on completion. Manual values are range checked and handed from the HTTP task to the clock loop. Offline time accounts for elapsed seconds. |
| High | Arduino build and target | The repository name and sketch filename differed, and the default ESP32-C3 partition rejected the linked image. The sketch now has a conventional Arduino directory, and the documented 4 MB configuration uses two 1,966,080-byte app slots. |

## Validation

| Check | Result |
| --- | --- |
| Original firmware, default ESP32-C3 partition | Linked, then failed the size check at 1,385,925 / 1,310,720 bytes. |
| Original firmware, ESP32-C3 Huge APP | Built. 1,385,957 bytes flash and 45,420 bytes static RAM. |
| Final firmware, ESP32-C3 Minimal SPIFFS | Built with Arduino-ESP32 3.3.7 and `--warnings all`. 1,380,721 / 1,966,080 bytes flash (70%) and 47,172 / 327,680 bytes static RAM (14%). |
| Host regressions | Passed with AddressSanitizer and UndefinedBehaviorSanitizer. Covers five arcade games, the OLED presenter, Calculator, EEPROM, Notes, Clock, Math, ADXL demos, acceleration sensor recovery, and GPIO debounce. |
| Static analysis | GCC 14 `-fanalyzer` and warnings-as-errors passed on the host regression translation units, including the production code extracted by the runner. |
| Extra host checks | Isolated BLE report tests covered axis saturation, stale samples, button release, wheel encoding, and advertising retry. A scheduler harness exercised app transitions and timer rollover. |

A fresh toolchain build can emit missing-field-initializer diagnostics in upstream Arduino/ESP-IDF headers. The final incremental target build reported zero diagnostics.

### Measured performance and remaining verification

- An emulated SSD1306 recorded **1,069 I²C write bytes** for a full refresh, **0** for an unchanged frame, and **18** for two distant changed bytes. That sparse case used **98.3% fewer bytes** than a full refresh. The counts exclude device address bytes and acknowledgments. The emulator also matched 800 randomized frames and checked recovery from command and data failures.
- Sending the 1,024 framebuffer bytes alone at 400 kHz takes at least 23.0 ms when each byte includes its ACK bit. Full-frame 60 FPS exceeds that bus budget. Smaller updates can fit within the Racer frame interval.
- The linker leaves 280,508 bytes for runtime allocation. That figure is a build result. Actual heap and stack high-water marks depend on the active app and radio use.
- Physical ESP32-C3 SuperMini testing remains pending, including sustained OLED FPS, flicker, I²C signal quality, tactile button and tilt response, Wi-Fi/NTP, BLE pairing, watchdog behavior, and runtime memory under radio activity.

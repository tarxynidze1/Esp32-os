# ESP32-OS

A menu-driven firmware for the ESP32-C3 SuperMini with a 128 × 64 SSD1306 OLED and five buttons. It includes Racer, DOOM raycasting, Tetris, Snake, Blocks (BLAST), a math quiz, a calculator, notes, Wi-Fi and a clock, a BLE tilt mouse, and two ADXL345 demos.

## Wiring

| Device | Connection |
| --- | --- |
| OLED and ADXL345 SDA | GPIO 8 |
| OLED and ADXL345 SCL | GPIO 9 |
| Up, Down, Left, Right, Select | GPIO 0, 1, 3, 4, 5 respectively, each switched to GND |
| SSD1306 address | `0x3C` |
| ADXL345 address | `0x53` with SDO low |

Use 3.3 V logic. The switches use internal pull-ups. The I²C bus uses pull-ups to 3.3 V and runs at 400 kHz. GPIO 8 and 9 are also boot-strapping pins. Some SuperMini variants connect an onboard LED to GPIO 8.

## Build and upload

Open `Esp32os/Esp32os.ino` in Arduino IDE. Install **esp32 by Espressif Systems 3.3.7**, **Adafruit GFX Library 1.13.0**, **Adafruit SSD1306 2.5.17**, and **Adafruit BusIO 1.17.4**. Select **ESP32C3 Dev Module**, **4 MB flash**, and **Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)**. The firmware needs the larger application slot.

From the repository root with Arduino CLI:

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.7 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install "Adafruit GFX Library@1.13.0" "Adafruit SSD1306@2.5.17" "Adafruit BusIO@1.17.4"
arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=min_spiffs --warnings all Esp32os
arduino-cli upload --port /dev/ttyACM0 --fqbn esp32:esp32:esp32c3:PartitionScheme=min_spiffs Esp32os
```

Use your board's serial port in the upload command. For host tests on Linux, install Python 3 and a C++11 compiler with AddressSanitizer and UndefinedBehaviorSanitizer, then run `python3 tests/run_host_tests.py`.

## Controls

- Tap Select to open or confirm. Hold Select for about 600 ms to exit an app. In BLAST placement mode, holding Select returns to shape selection first.
- In Racer, tilt the ADXL345 or hold Left or Right to steer. Tap Select after a crash to restart.
- In the calculator, enter `1=` to open the scientific keypad. `C` returns to the standard keypad.
- In Wi-Fi, use `P` to cycle keyboard pages, `Eye` to show or mask the password, `<` to delete, and `>` to connect. After opening Wi-Fi, join the `ESP32-C3-Clock` access point and open `http://192.168.4.1/` to set the clock manually. A station connection can synchronize via NTP in the configured Moscow time zone.

Racer updates its simulation at 60 Hz and schedules up to 60 OLED updates per second in fullscreen. Actual display throughput depends on how much of the framebuffer changes. [Repair and validation report](REPAIR_REPORT.md)

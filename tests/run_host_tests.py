#!/usr/bin/env python3
"""Run game regressions with deterministic clock, buttons and random inputs."""

import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SKETCH = ROOT / "Esp32os" / "Esp32os.ino"
TESTS = ROOT / "tests"


def game_logic():
    # Compile the sketch's actual game functions with platform dependencies
    # supplied by game_state_test.cpp. Keep rendering and drivers on-device.
    sketch = SKETCH.read_text(encoding="utf-8")
    types = [
        re.search(rf"^(?:enum|struct) {name}\b[^\n]*", sketch, re.MULTILINE)[0]
        for name in ("BlockState", "BlockShape", "SnakePt", "Player", "Entity")
    ]
    lines = sketch.splitlines()
    start = next(i for i, line in enumerate(lines) if line.startswith("uint16_t tBrd"))
    end = next(i for i, line in enumerate(lines) if line.startswith("void notesInit() {"))
    loops = next(i for i, line in enumerate(lines) if line.startswith("void loopTetris() {"))
    loops_end = next(i for i, line in enumerate(lines) if line.startswith("void loopBLE() {"))
    doom_start = sketch.index("#define MAP_SIZE ")
    doom_init = sketch.index("void doomInit() {", doom_start)
    doom_end = sketch.index("\n}", doom_init) + 2
    return "\n".join([*types, *lines[start:end], *lines[loops:loops_end],
                      sketch[doom_start:doom_end]])


def oled_presenter():
    sketch = SKETCH.read_text(encoding="utf-8")
    start = sketch.index("constexpr uint8_t OLED_COLUMNS =")
    end = sketch.index("void drawMenuIcon(", start)
    return sketch[start:end]


def tool_logic():
    sketch = SKETCH.read_text(encoding="utf-8")
    sections = [
        ("#define MAX_NOTES ", "// ─── 2."),
        ("Note notes[MAX_NOTES]", "// ─── 4."),
        ("constexpr char ClockTimezone", "float cameraX"),
        ("const char KEYS_NORM", "RacerGame racer"),
        ("void notesInit() {", "// ─── DOOM"),
        ("int noteCount() {", "const char* noteDisplayTitle()"),
        ("void enterAdxlApp() {", "void loopDoom() {"),
        ("void startNoteEdit(", "void updateToolAnimations() {"),
    ]
    return "\n".join(sketch[sketch.index(start):sketch.index(end, sketch.index(start))]
                     for start, end in sections)


def hardware_io():
    sketch = SKETCH.read_text(encoding="utf-8")
    sensor_start = sketch.index("constexpr uint8_t ADXL345_ADDR")
    setup_start = sketch.index("bool initHardware() {", sensor_start)
    debounce_start = sketch.index("void updateButtons() {", setup_start)
    debounce_end = sketch.index("constexpr char ClockTimezone", debounce_start)
    return sketch[sensor_start:setup_start] + sketch[debounce_start:debounce_end]


def main():
    compiler = os.environ.get("CXX", "c++")
    with tempfile.TemporaryDirectory() as tmp:
        build = Path(tmp)
        (build / "game_logic_under_test.h").write_text(game_logic(), encoding="utf-8")
        (build / "oled_presenter_under_test.h").write_text(oled_presenter(), encoding="utf-8")
        (build / "tool_logic_under_test.h").write_text(tool_logic(), encoding="utf-8")
        (build / "hardware_io_under_test.h").write_text(hardware_io(), encoding="utf-8")
        for name in ("racer_test", "game_state_test", "oled_presenter_test", "tool_state_test",
                     "hardware_io_test"):
            binary = build / name
            subprocess.run(
                [compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                 "-fsanitize=address,undefined", "-no-pie", "-fno-sanitize-recover=undefined",
                 "-I", str(SKETCH.parent), "-I", str(build), str(TESTS / f"{name}.cpp"),
                 "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()

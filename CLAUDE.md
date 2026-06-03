# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A collection of Arduino sketches for synthesiser experiments targeting the **ESP32-C3 Super Mini with 0.42" OLED**. All audio sketches drive a MAX98357A I²S amp for sound output.

The full wiring map, per-sketch summary, build commands, and datasheet references live in [`README.md`](README.md) — read it before assuming any pin assignment or device.

## Board

- **ESP32-C3 Super Mini with 0.42" OLED**
  - ESP32-C3, single-core RISC-V, 160 MHz, **no FPU** → use integer fixed-point at audio rate
  - 0.42" SSD1306 OLED, 72×40 visible on a 128×64 framebuffer (use the U8g2 `U8G2_SSD1306_72X40_ER_F_HW_I2C` constructor — it handles the offset)
  - Native USB-CDC + USB-JTAG (no USB-UART driver needed on macOS)
  - Built-in LED on GPIO 8 (**active-low**)
  - BOOT button on GPIO 9
  - Pads labeled `RX`/`TX` are GPIO 20 / GPIO 21
  - Strapping pins: GPIO 2, 8, 9 — avoid for inputs that idle LOW
  - ADC1 channels are GPIO 0–4 only

## Repository structure

Each sketch lives in its own folder matching the sketch name (Arduino IDE requirement):

```
<SketchName>/
  <SketchName>.ino
```

## Build & upload

`arduino-cli` is the build path; Arduino IDE works equivalently.

**FQBN must include `CDCOnBoot=cdc`** — without it, `Serial.print` goes to UART0 (GPIO 20/21) and the serial monitor stays silent.

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32c3:CDCOnBoot=cdc" <SketchName>/
arduino-cli board list                # port re-enumerates on every replug
arduino-cli upload -p /dev/cu.usbmodem<NNN> --fqbn "esp32:esp32:esp32c3:CDCOnBoot=cdc" <SketchName>/
arduino-cli monitor -p /dev/cu.usbmodem<NNN> --config baudrate=115200
```

## ESP32-specific APIs in use

- **LEDC (LED PWM Controller)**: `ledcAttach()`, `ledcFade()`, `ledcFadeWithInterrupt()` — ESP32 Arduino core v3+ API
- **ISR handlers**: must be annotated with `ARDUINO_ISR_ATTR`
- **I²S (ESP32 core 3.x)**: `ESP_I2S.h` — `I2SClass.setPins(bclk, ws, dout)` then `begin(I2S_MODE_STD, sample_rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)`
- **OLED**: U8g2 in HW-I²C mode

## Recurring gotchas (also captured in README's "Notes for Claude" section)

- **No FPU**: per-sample `sinf()` is too slow. Pre-compute LUTs in `setup()` or use Q8/Q16 fixed-point.
- **Aliasing**: naive saw/square at 22 kHz fold harmonics back as buzz. Either band-limit (Fourier series) or filter aggressively.
- **Buffer-boundary clicks**: snapshotting a parameter once per audio buffer and reusing it for 1024+ samples creates ~22 Hz click trains during transitions. Interpolate per-sample.
- **Arduino auto-prototypes**: functions in `.ino` that take user-defined struct types fail to compile because the prototype is inserted before the struct definition. Use per-voice functions or move structs into a `.h`.
- **Pin-name collisions**: ESP32 core defines `A0`, `A1`, `A2`… as analog-pin macros — name musical notes `NOTE_A2` etc.
- **Port re-enumeration**: `/dev/cu.usbmodem<NNN>` trailing digits change every replug. Always run `arduino-cli board list`.
- **Hardware-first debugging on new audio modules**: when a brand-new amp produces noise/clicks, suggest swapping the chip *before* multiple rounds of software optimization.

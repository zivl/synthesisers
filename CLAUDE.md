# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a collection of Arduino sketches for synthesiser-related projects, targeting the **ESP32-2432S028R (CYD — Cheap Yellow Display)** board. The primary IDE is Arduino IDE.

## Target Hardware

- **Board**: ESP32-2432S028R (CYD)
  - ESP32 dual-core 240MHz
  - 2.8" TFT LCD display (ILI9341, 320×240)
  - Integrated resistive touchscreen (XPT2046)
  - Built-in RGB LED (active-low, on GPIO 4/16/17)
  - microSD card slot
  - USB-C connector
- **Serial baud rate**: 115200

## Repository Structure

Each sketch lives in its own folder matching the sketch name (Arduino IDE requirement):

```
<SketchName>/
  <SketchName>.ino   # Main sketch file
```

## Arduino IDE Setup

This repo targets Arduino IDE (not PlatformIO). To work with these sketches:

1. Open the `.ino` file inside its folder in Arduino IDE
2. Select board: **ESP32 Dev Module** (or the CYD-specific board if installed)
3. Upload via USB; monitor serial output at 115200 baud

There is no automated build/lint/test pipeline — compilation and upload happen through the Arduino IDE GUI or `arduino-cli`.

### arduino-cli equivalents

```bash
# Compile a sketch (replace board FQBN as needed)
arduino-cli compile --fqbn esp32:esp32:esp32 LEDCFade/

# Upload (use 115200 baud — default 921600 fails on this board)
arduino-cli upload -p /dev/cu.usbserial-* --fqbn esp32:esp32:esp32 --upload-property upload.speed=115200 LEDCFade/

# Monitor serial output
arduino-cli monitor -p /dev/cu.usbserial-* --config baudrate=115200
```

## ESP32-Specific APIs in Use

- **LEDC (LED PWM Controller)**: `ledcAttach()`, `ledcFade()`, `ledcFadeWithInterrupt()` — ESP32 Arduino core v3+ API
- **ISR handlers**: Must be annotated with `ARDUINO_ISR_ATTR`
- **TFT display**: Likely uses [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) library configured for ILI9341 + CYD pin mapping
- **Touch**: XPT2046 on a **separate HSPI bus** (CLK=25, MISO=39, MOSI=32, CS=33, IRQ=36) — must be initialised with `SPIClass(HSPI)` and `touch.begin(touchSPI)`, not the default SPI

## CYD Pin Reference

| Function       | GPIO     |
|----------------|----------|
| TFT CS         | 15       |
| TFT DC/RS      | 2        |
| TFT RST        | —        |
| TFT MOSI       | 13       |
| TFT MISO       | 12       |
| TFT CLK        | 14       |
| TFT Backlight  | 21       |
| Touch CS       | 33       |
| Touch IRQ      | 36       |
| Touch CLK      | 25       |
| Touch MISO     | 39       |
| Touch MOSI     | 32       |
| RGB LED R      | 4 (inv)  |
| RGB LED G      | 16 (inv) |
| RGB LED B      | 17 (inv) |
| SD CS          | 5        |

# synthesisers

Arduino sketches for ESP32-based synth and visualiser experiments. Each sketch lives in its own folder matching the sketch name (Arduino IDE requirement).

## Boards

Two boards are targeted in this repo. Most recent work is on the C3.

### ESP32-C3 Super Mini with 0.42" OLED (current focus)
- ESP32-C3, single-core RISC-V, 160 MHz, **no FPU**
- 0.42" SSD1306 OLED: 72×40 visible pixels offset on a 128×64 framebuffer
- Native USB-CDC + USB-JTAG (no external USB-UART chip, no driver needed on macOS)
- Built-in LED on **GPIO 8 (active-low)**
- BOOT button on **GPIO 9**
- Pads labeled `RX` and `TX` on the board correspond to **GPIO 20** and **GPIO 21**

### ESP32-2432S028R "CYD" (Cheap Yellow Display)
- ESP32 dual-core, 240 MHz
- 2.8" TFT (ILI9341, 320×240), resistive touch (XPT2046)
- Built-in RGB LED, microSD slot
- CYD-specific pin map lives in [`CLAUDE.md`](CLAUDE.md)

---

## Wiring map — ESP32-C3 Super Mini

The current audio sketches (`Acid303`, `DarkSynth`, `Disco80`, `WaveMorphAudio`) target this exact wiring.

### Power rails (use the breadboard's rails)

| Rail | Source on C3 |
|---|---|
| 5V | `5V` pin (USB bus power) |
| 3V3 | `3V3` pin |
| GND | `GND` pin |

### Signal-direction rule

> **Anything that drives a signal INTO the C3 must run from 3V3.** The C3's GPIOs are not 5V-tolerant. The amp is the only 5V device here because the C3 *sends* I²S to it (3.3V logic, which the amp accepts as HIGH); its 5V VIN is independent of its logic inputs.

### OLED (SSD1306, 72×40, I²C)

| OLED pin | C3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | **GPIO 5** |
| SCL | **GPIO 6** |

### MAX98357A I²S mono amp

| Amp pin | C3 | Notes |
|---|---|---|
| VIN | **5V** | 3V3 works per datasheet but in practice the Super Mini's onboard 3V3 regulator can't deliver enough current for audible output |
| GND | GND | |
| BCLK | **GPIO 7** | |
| LRC | **GPIO 10** | |
| DIN | **GPIO 20 (RX pad)** | |
| GAIN | floating | 9 dB default. 100 kΩ to VDD = 12 dB, direct to VDD = 15 dB, direct to GND = 6 dB, 100 kΩ to GND = 3 dB |
| SD | floating | Internal pull-up enables "play L+R mix" mode. **Do NOT tie to GND** (= shutdown) |
| SPK+ | speaker + | |
| SPK− | speaker − | Bridged output — **do NOT ground** |

### 4× B10k pots (linear taper, 10 kΩ)

Each pot is identical: one outer leg → 3V3, other outer leg → GND, wiper → ADC GPIO. If a pot feels inverted (CW = quieter), swap its two outer legs.

| Pot | Function (Acid303) | Wiper → |
|---|---|---|
| #1 | Cutoff | **GPIO 0** |
| #2 | Resonance | **GPIO 1** |
| #3 | Env Mod | **GPIO 3** |
| #4 | Decay | **GPIO 4** |

### 2× HW-763 capacitive touch modules (TTP223)

VCC must be **3V3** — the module drives its OUT pin at VCC level and the C3's GPIO is not 5V tolerant.

| Module | VCC | GND | OUT → | Special |
|---|---|---|---|---|
| Touch 1 (Octave Up) | 3V3 | GND | **GPIO 21 (TX pad)** | none |
| Touch 2 (Pattern Variant) | 3V3 | GND | **GPIO 2** | Bridge the **"A" solder pad** on the back of the module to flip it to active-LOW. GPIO 2 is a boot strapping pin and the default active-HIGH idle would keep the chip from booting. |

### Full GPIO usage

| GPIO | Use | Notes |
|---|---|---|
| 0 | Pot 1 — Cutoff | ADC1_CH0 |
| 1 | Pot 2 — Resonance | ADC1_CH1 |
| 2 | Touch 2 — Pattern Variant | ADC1_CH2; **strapping pin** — must read HIGH at boot |
| 3 | Pot 3 — Env Mod | ADC1_CH3 |
| 4 | Pot 4 — Decay | ADC1_CH4 |
| 5 | OLED SDA | also ADC2_CH0 but avoid (Wi-Fi shared) |
| 6 | OLED SCL | |
| 7 | I²S BCLK | |
| 8 | Onboard LED | **strapping pin**, active-low |
| 9 | BOOT button | **strapping pin**, LOW at boot = download mode |
| 10 | I²S LRC | |
| 20 (RX) | I²S DIN | UART0 RX — free when `CDCOnBoot=cdc` routes Serial to USB |
| 21 (TX) | Touch 1 — Octave Up | UART0 TX — same |

---

## Sketches

| Sketch | Board | Description |
|---|---|---|
| `HelloWorld/` | any | First serial smoke test |
| `BlinkC3/` | C3 | LED blink — note GPIO 8 is active-low |
| `LEDCFade/` | ESP32 | LEDC PWM fade |
| `OLEDHello/` | C3 | 0.42" OLED hello via U8g2 (handles panel offset) |
| `WaveMorph/` | C3 | Sine → saw → square morph visualised on the OLED |
| `WaveMorphAudio/` | C3 | Same morph made audible via MAX98357A. Uses band-limited Fourier-series saw/square + audio-rate integer crossfade to eliminate aliasing and buffer-boundary clicks |
| `Disco80/` | C3 | 118 BPM four-on-the-floor disco loop: pitched-sine kick with pitch sweep, noise hi-hat, octave-jumper bass, sine lead riff |
| `DarkSynth/` | C3 | Slow synthwave drone in D minor through a 1-pole LPF — three saw voices, long decays, a 13.5 s loop |
| `Acid303/` | C3 | TB-303-style acid bassline: saw oscillator → 2-pole Chamberlin state-variable resonant LPF with filter envelope, slides, accents. Live control via the 4 pots; touch sensors for octave-up and pattern variant |
| `DiscoBall/`, `FSharpTone/` | CYD | Earlier display/touch experiments |

---

## Build & upload

`arduino-cli` is the build path. Arduino IDE works too with the same FQBN.

### FQBN
**Always use `CDCOnBoot=cdc` for the C3 sketches** — without it, `Serial.print` is routed to UART0 (GPIO 20/21) instead of USB CDC, and the serial monitor stays silent.

```bash
# Compile (C3)
arduino-cli compile --fqbn "esp32:esp32:esp32c3:CDCOnBoot=cdc" Acid303/

# Find the port (re-enumerates each replug)
arduino-cli board list

# Upload
arduino-cli upload -p /dev/cu.usbmodem<NNN> --fqbn "esp32:esp32:esp32c3:CDCOnBoot=cdc" Acid303/

# Serial monitor
arduino-cli monitor -p /dev/cu.usbmodem<NNN> --config baudrate=115200
```

### Required libraries
- `U8g2` — install with `arduino-cli lib install "U8g2"`
- `ESP_I2S` — bundled with ESP32 Arduino core 3.x
- `Wire` — bundled

### Port re-enumeration on macOS
The C3's native USB shows up as `/dev/cu.usbmodem<NNN>`. The trailing digits **change every time the board is unplugged** (e.g. `usbmodem11101` → `usbmodem1101` → `usbmodem101`). Always re-check `arduino-cli board list` after replugging.

### CYD board
Different FQBN and special baud rate:
```bash
arduino-cli compile --fqbn esp32:esp32:esp32 <CYDSketch>/
arduino-cli upload -p /dev/cu.usbserial-* --fqbn esp32:esp32:esp32 --upload-property upload.speed=115200 <CYDSketch>/
```

---

## References

### Boards & cores
- ESP32 Arduino core: https://github.com/espressif/arduino-esp32
- ESP32-C3 datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf
- ESP32-C3 technical reference manual: https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf

### Audio
- MAX98357A datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf
- Adafruit MAX98357A guide (excellent for wiring & gain pin behaviour): https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp

### Display
- SSD1306 datasheet: https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf
- U8g2 library: https://github.com/olikraus/u8g2 — use `U8G2_SSD1306_72X40_ER_F_HW_I2C` constructor for the 0.42" panel; it handles the column/page offset automatically

### Input
- TTP223 datasheet (HW-763 touch module): https://www.tontek.com.tw/uploads/product/74/TTP223C-BA6_V1.2.pdf
  - Solder pad **A** flips the output polarity (default active-HIGH → active-LOW)
  - Solder pad **B** switches between momentary and toggle behaviour

---

## Notes for Claude (self-context for future sessions)

When resuming this project:

1. **Wiring map above is the source of truth** — don't re-derive pin assignments from scratch.
2. **`CDCOnBoot=cdc` is mandatory** in the FQBN for C3 audio sketches, otherwise Serial output is invisible.
3. **C3 has no FPU.** Audio-rate code uses integer fixed-point math. Float math per-sample (especially `sinf`) is too slow — pre-compute LUTs in `setup()` or use Q8/Q16 fixed-point.
4. **Aliasing is real.** Naive saw/square at 22 kHz sample rate sounds buzzy because high harmonics fold back. Either band-limit (see `WaveMorphAudio`'s Fourier-series tables) or filter aggressively.
5. **Audio-rate parameter changes need per-sample interpolation.** Snapshotting a parameter once per buffer and using it for the whole 1024–2048 sample chunk creates audible clicks at buffer boundaries (~22 Hz click train sounds like grain). See `WaveMorphAudio` for the integer-blend pattern.
6. **Hardware-first debugging on new audio modules.** When a brand-new amp produces noise/clicks/grain, suggest swapping the module *before* multiple rounds of software optimization. See private memory `hardware-first-debug.md`.
7. **Arduino IDE auto-generates function prototypes** at the top of `.ino` files. Functions taking user-defined struct types as parameters fail to compile because the prototype is placed before the struct definition. Workaround: don't pass structs as parameters in `.ino` files — give each voice its own dedicated function (`process_bass()`, `process_lead()` etc.), or move the struct to a `.h` file.
8. **Pin-name conflicts.** ESP32 Arduino core defines `A0`, `A1`, `A2`… as analog-pin macros. Don't name musical-note constants `A2`, `A3` etc. — use `NOTE_A2`, `NOTE_A3`.
9. **Port re-enumerates on replug** — always run `arduino-cli board list` before uploading after disconnecting the board.
10. **CYD specifics live in `CLAUDE.md`** — don't confuse them with C3 specifics here.

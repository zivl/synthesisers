// Rainbow cycle on CYD RGB LED + LCD screen with touch controls
// RGB LED: R=GPIO4, G=GPIO16, B=GPIO17 (active-low)
// LCD: ILI9341 320x240 via TFT_eSPI
// Touch: XPT2046 on GPIO33 (CS), GPIO36 (IRQ)

#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

// ── RGB LED ──────────────────────────────────────────────────────────────────
#define LED_R 4
#define LED_G 16
#define LED_B 17
#define LEDC_FREQ 5000
#define LEDC_RES  12
#define MAX_DUTY  4095

// ── Touch (HSPI bus — separate from TFT VSPI) ─────────────────────────────────
#define TOUCH_CS_PIN  33
#define TOUCH_IRQ     36
#define TOUCH_CLK     25
#define TOUCH_MISO    39
#define TOUCH_MOSI    32

// ── Tuneable parameters (adjusted via on-screen buttons) ─────────────────────
int cycleDuration = 4000;   // ms for one full rainbow cycle  (200 – 20000)
int stepDelay     = 10;     // ms per hue step                (1 – 100)

// ── Display / touch objects ───────────────────────────────────────────────────
TFT_eSPI tft;
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ);

// ── Layout constants ─────────────────────────────────────────────────────────
// Screen is 320 x 240 (landscape).
// Top 160 px → rainbow gradient band
// Bottom 80 px → two parameter rows

#define RAINBOW_H   160
#define CTRL_Y      160   // top of controls area
#define CTRL_H       80
#define ROW1_Y      (CTRL_Y + 8)
#define ROW2_Y      (CTRL_Y + 44)
#define BTN_W        40
#define BTN_H        28
#define BTN_MINUS_X   8
#define BTN_PLUS_X  270
#define LABEL_X      58
#define VALUE_X     160   // centre of value text

// Touch calibration for CYD (portrait raw → landscape screen coords)
// Raw range is roughly x: 200–3900, y: 200–3900
#define TS_MINX 200
#define TS_MAXX 3900
#define TS_MINY 200
#define TS_MAXY 3900

// ── State ─────────────────────────────────────────────────────────────────────
float   currentHue  = 0;
uint32_t lastStep   = 0;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Convert hue (0–360) to a 16-bit colour (RGB565) at full saturation/value
uint16_t hueToRGB565(float hue) {
  float h = hue / 60.0f;
  int   i = (int)h % 6;
  float f = h - (int)h;
  float q = 1.0f - f;
  float r, g, b;
  switch (i) {
    case 0: r=1; g=f; b=0; break;
    case 1: r=q; g=1; b=0; break;
    case 2: r=0; g=1; b=f; break;
    case 3: r=0; g=q; b=1; break;
    case 4: r=f; g=0; b=1; break;
    default: r=1; g=0; b=q; break;
  }
  return tft.color565((uint8_t)(r*255), (uint8_t)(g*255), (uint8_t)(b*255));
}

// Write RGB values to the physical LED (active-low)
void setLedHue(float hue) {
  float h = hue / 60.0f;
  int   i = (int)h % 6;
  float f = h - (int)h;
  float q = 1.0f - f;
  float r, g, b;
  switch (i) {
    case 0: r=1; g=f; b=0; break;
    case 1: r=q; g=1; b=0; break;
    case 2: r=0; g=1; b=f; break;
    case 3: r=0; g=q; b=1; break;
    case 4: r=f; g=0; b=1; break;
    default: r=1; g=0; b=q; break;
  }
  ledcWrite(LED_R, MAX_DUTY - (uint32_t)(r * MAX_DUTY));
  ledcWrite(LED_G, MAX_DUTY - (uint32_t)(g * MAX_DUTY));
  ledcWrite(LED_B, MAX_DUTY - (uint32_t)(b * MAX_DUTY));
}

// Draw the full-width rainbow gradient (called once, then scrolled via hue offset)
void drawRainbow(float offsetHue) {
  for (int x = 0; x < 320; x++) {
    float hue = fmod(offsetHue + (360.0f * x / 320.0f), 360.0f);
    uint16_t c = hueToRGB565(hue);
    tft.drawFastVLine(x, 0, RAINBOW_H, c);
  }
}

// Draw a single button
void drawButton(int x, int y, int w, int h, const char* label, uint16_t bg) {
  tft.fillRoundRect(x, y, w, h, 6, bg);
  tft.drawRoundRect(x, y, w, h, 6, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString(label, x + w/2, y + h/2);
}

// Draw the controls area (bottom 80 px)
void drawControls() {
  tft.fillRect(0, CTRL_Y, 320, CTRL_H, TFT_BLACK);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(1);

  // Row 1 – Cycle duration
  drawButton(BTN_MINUS_X, ROW1_Y, BTN_W, BTN_H, "-", TFT_NAVY);
  drawButton(BTN_PLUS_X,  ROW1_Y, BTN_W, BTN_H, "+", TFT_NAVY);
  tft.drawString("Cycle ms", LABEL_X, ROW1_Y + BTN_H/2);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", cycleDuration);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(buf, VALUE_X, ROW1_Y + BTN_H/2);

  // Row 2 – Step delay
  drawButton(BTN_MINUS_X, ROW2_Y, BTN_W, BTN_H, "-", TFT_DARKGREEN);
  drawButton(BTN_PLUS_X,  ROW2_Y, BTN_W, BTN_H, "+", TFT_DARKGREEN);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("Step ms ", LABEL_X, ROW2_Y + BTN_H/2);
  snprintf(buf, sizeof(buf), "%d", stepDelay);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(buf, VALUE_X, ROW2_Y + BTN_H/2);
}

// Map raw touch coordinates to screen pixels (landscape)
bool getTouchPoint(int &sx, int &sy) {
  if (!touch.touched()) return false;
  TS_Point p = touch.getPoint();
  // CYD touch is mounted rotated: swap & invert to match landscape display
  sx = map(p.y, TS_MINY, TS_MAXY, 0, 320);
  sy = map(p.x, TS_MINX, TS_MAXX, 240, 0);
  return (sx >= 0 && sx < 320 && sy >= 0 && sy < 240);
}

bool hitButton(int tx, int ty, int bx, int by, int bw, int bh) {
  return (tx >= bx && tx <= bx + bw && ty >= by && ty <= by + bh);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // RGB LED
  ledcAttach(LED_R, LEDC_FREQ, LEDC_RES);
  ledcAttach(LED_G, LEDC_FREQ, LEDC_RES);
  ledcAttach(LED_B, LEDC_FREQ, LEDC_RES);

  // Display
  tft.init();
  tft.setRotation(1);   // landscape, USB on left
  tft.fillScreen(TFT_BLACK);

  // Touch — initialise on its own HSPI bus (CYD uses separate buses for TFT and touch)
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS_PIN);
  touch.begin(touchSPI);
  touch.setRotation(1);

  drawRainbow(0);
  drawControls();
  Serial.println("Ready.");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  // ── Advance hue on timer ─────────────────────────────────────────────────
  if (now - lastStep >= (uint32_t)stepDelay) {
    lastStep = now;

    int steps = max(1, cycleDuration / stepDelay);
    float hueStep = 360.0f / steps;
    currentHue = fmod(currentHue + hueStep, 360.0f);

    setLedHue(currentHue);
    drawRainbow(currentHue);
  }

  // ── Handle touch ─────────────────────────────────────────────────────────
  if (touch.tirqTouched()) {
    int tx, ty;
    if (getTouchPoint(tx, ty)) {
      bool changed = false;

      // Row 1 — Cycle duration
      if (hitButton(tx, ty, BTN_MINUS_X, ROW1_Y, BTN_W, BTN_H)) {
        cycleDuration = max(200, cycleDuration - 200);
        changed = true;
      } else if (hitButton(tx, ty, BTN_PLUS_X, ROW1_Y, BTN_W, BTN_H)) {
        cycleDuration = min(20000, cycleDuration + 200);
        changed = true;
      }

      // Row 2 — Step delay
      if (hitButton(tx, ty, BTN_MINUS_X, ROW2_Y, BTN_W, BTN_H)) {
        stepDelay = max(1, stepDelay - 1);
        changed = true;
      } else if (hitButton(tx, ty, BTN_PLUS_X, ROW2_Y, BTN_W, BTN_H)) {
        stepDelay = min(100, stepDelay + 1);
        changed = true;
      }

      if (changed) {
        drawControls();
        delay(150);   // simple debounce
      }
    }
  }
}

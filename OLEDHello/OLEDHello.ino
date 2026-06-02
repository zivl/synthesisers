#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>

U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

static const int W = 72;
static const int H = 40;
static const int WAVE_TOP = 12;
static const int WAVE_BOTTOM = H - 1;
static const int WAVE_MID = (WAVE_TOP + WAVE_BOTTOM) / 2;
static const int WAVE_AMP = (WAVE_BOTTOM - WAVE_TOP) / 2 - 1;

static const float CYCLES_ACROSS = 2.0f;
static const float K = (2.0f * PI * CYCLES_ACROSS) / W;
static const float PHASE_STEP = 0.25f;

void setup() {
  Serial.begin(115200);
  Wire.begin(5, 6);
  Wire.setClock(400000);
  u8g2.begin();
}

void loop() {
  static float phase = 0.0f;

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setCursor(0, 7);
  u8g2.print("ESP32-C3 sin");

  int prev_y = WAVE_MID - (int)(WAVE_AMP * sinf(phase));
  for (int x = 1; x < W; x++) {
    int y = WAVE_MID - (int)(WAVE_AMP * sinf(K * x + phase));
    u8g2.drawLine(x - 1, prev_y, x, y);
    prev_y = y;
  }

  u8g2.sendBuffer();

  phase += PHASE_STEP;
  if (phase > 2.0f * PI) phase -= 2.0f * PI;
}

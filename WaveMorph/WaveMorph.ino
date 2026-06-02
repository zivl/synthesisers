#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>

U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

static const int W = 72;
static const int H = 40;
static const int WAVE_TOP = 10;
static const int WAVE_BOTTOM = H - 1;
static const int WAVE_MID = (WAVE_TOP + WAVE_BOTTOM) / 2;
static const int WAVE_AMP = (WAVE_BOTTOM - WAVE_TOP) / 2 - 1;

static const float TWO_PI_F = 2.0f * (float)PI;
static const float CYCLES_ACROSS = 2.0f;
static const float K = (TWO_PI_F * CYCLES_ACROSS) / W;
static const float PHASE_STEP = 0.20f;
static const float MORPH_STEP = 0.012f;

static inline float wave_sin(float a) { return sinf(a); }
static inline float wave_saw(float a) {
  float p = a / TWO_PI_F;
  p -= floorf(p);
  return 2.0f * p - 1.0f;
}
static inline float wave_sqr(float a) { return sinf(a) >= 0.0f ? 1.0f : -1.0f; }

static const char* shape_name(int idx) {
  switch (idx % 3) {
    case 0:  return "SIN";
    case 1:  return "SAW";
    default: return "SQR";
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(5, 6);
  Wire.setClock(400000);
  u8g2.begin();
}

void loop() {
  static float phase = 0.0f;
  static float morph = 0.0f;

  int seg = (int)morph;
  float t = morph - seg;
  const char* label = (t < 0.5f) ? shape_name(seg) : shape_name(seg + 1);

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setCursor(0, 7);
  u8g2.print(label);

  const int bar_x = 36;
  const int bar_w = W - bar_x;
  u8g2.drawFrame(bar_x, 1, bar_w, 7);
  int fill = (int)((morph / 3.0f) * (bar_w - 2) + 0.5f);
  if (fill > 0) u8g2.drawBox(bar_x + 1, 2, fill, 5);

  int prev_y = 0;
  for (int x = 0; x < W; x++) {
    float a = K * (float)x + phase;
    float ya, yb;
    switch (seg) {
      case 0:  ya = wave_sin(a); yb = wave_saw(a); break;
      case 1:  ya = wave_saw(a); yb = wave_sqr(a); break;
      default: ya = wave_sqr(a); yb = wave_sin(a); break;
    }
    float y_norm = ya * (1.0f - t) + yb * t;
    int y = WAVE_MID - (int)(WAVE_AMP * y_norm);
    if (y < WAVE_TOP)    y = WAVE_TOP;
    if (y > WAVE_BOTTOM) y = WAVE_BOTTOM;
    if (x == 0) prev_y = y;
    else {
      u8g2.drawLine(x - 1, prev_y, x, y);
      prev_y = y;
    }
  }

  u8g2.sendBuffer();

  phase += PHASE_STEP;
  if (phase > TWO_PI_F) phase -= TWO_PI_F;
  morph += MORPH_STEP;
  if (morph >= 3.0f) morph -= 3.0f;
}

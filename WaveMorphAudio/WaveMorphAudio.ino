#include <Wire.h>
#include <U8g2lib.h>
#include <ESP_I2S.h>
#include <math.h>

U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
I2SClass i2s;

constexpr int I2S_BCLK = 4;
constexpr int I2S_LRC  = 3;
constexpr int I2S_DIN  = 1;

constexpr int SAMPLE_RATE = 22050;
constexpr int TONE_HZ = 220;
constexpr int AUDIO_BUF_SAMPLES = 1024;
constexpr int PERIOD_SAMPLES = SAMPLE_RATE / TONE_HZ;
int16_t audioBuf[AUDIO_BUF_SAMPLES];

int16_t sinBL[PERIOD_SAMPLES];
int16_t sawBL[PERIOD_SAMPLES];
int16_t sqrBL[PERIOD_SAMPLES];
constexpr float AMP_FULLSCALE = 11000.0f;

constexpr int W = 72;
constexpr int H = 40;
constexpr int WAVE_TOP = 10;
constexpr int WAVE_BOTTOM = H - 1;
constexpr int WAVE_MID = (WAVE_TOP + WAVE_BOTTOM) / 2;
constexpr int WAVE_AMP = (WAVE_BOTTOM - WAVE_TOP) / 2 - 1;

constexpr float TWO_PI_F = 2.0f * (float)PI;
constexpr float DISPLAY_K = (TWO_PI_F * 2.0f) / W;
constexpr float DISPLAY_PHASE_STEP = 0.20f;

constexpr bool MORPH_ENABLED = false;

constexpr float HOLD_SEC = 2.5f;
constexpr float GLIDE_SEC = 2.0f;
constexpr uint32_t HOLD_SAMPLES = (uint32_t)(HOLD_SEC * SAMPLE_RATE);
constexpr uint32_t GLIDE_SAMPLES = (uint32_t)(GLIDE_SEC * SAMPLE_RATE);
constexpr uint32_t SEG_SAMPLES = HOLD_SAMPLES + GLIDE_SAMPLES;
constexpr uint32_t CYCLE_SAMPLES = 3 * SEG_SAMPLES;
constexpr uint32_t T_Q_STEP = (256u << 16) / GLIDE_SAMPLES;

static inline float wave_sin(float a) { return sinf(a); }
static inline float wave_saw(float a) {
  float p = a / TWO_PI_F;
  p -= floorf(p);
  return 2.0f * p - 1.0f;
}
static inline float wave_sqr(float a) { return sinf(a) >= 0.0f ? 1.0f : -1.0f; }

static float blend_display(int seg, float t, float a) {
  float ya, yb;
  switch (seg) {
    case 0:  ya = wave_sin(a); yb = wave_saw(a); break;
    case 1:  ya = wave_saw(a); yb = wave_sqr(a); break;
    default: ya = wave_sqr(a); yb = wave_sin(a); break;
  }
  return ya * (1.0f - t) + yb * t;
}

static const char* shape_name(int idx) {
  switch (idx % 3) {
    case 0:  return "SIN";
    case 1:  return "SAW";
    default: return "SQR";
  }
}

static int16_t* pick_a(int seg) {
  switch (seg) { case 0: return sinBL; case 1: return sawBL; default: return sqrBL; }
}
static int16_t* pick_b(int seg) {
  switch (seg) { case 0: return sawBL; case 1: return sqrBL; default: return sinBL; }
}

enum Phase { PHASE_HOLD, PHASE_GLIDE };

int period_pos = 0;
float display_phase = 0.0f;
int current_seg = 0;
Phase current_phase = PHASE_HOLD;
uint32_t samples_left_in_phase = HOLD_SAMPLES;
uint32_t t_q_accum = 0;
uint32_t total_samples = 0;
int16_t* table_a = sinBL;
int16_t* table_b = sawBL;

static void build_bandlimited_tables() {
  const int max_h = (SAMPLE_RATE / 2) / TONE_HZ;
  float saw_buf[PERIOD_SAMPLES];
  float sqr_buf[PERIOD_SAMPLES];
  float saw_peak = 0.0f, sqr_peak = 0.0f;

  for (int i = 0; i < PERIOD_SAMPLES; i++) {
    float a = TWO_PI_F * (float)i / (float)PERIOD_SAMPLES;
    sinBL[i] = (int16_t)(sinf(a) * AMP_FULLSCALE);

    float s = 0.0f, q = 0.0f;
    for (int h = 1; h <= max_h; h++) {
      s += sinf(h * a) / (float)h;
    }
    for (int h = 1; h <= max_h; h += 2) {
      q += sinf(h * a) / (float)h;
    }
    saw_buf[i] = s;
    sqr_buf[i] = q;
    if (fabsf(s) > saw_peak) saw_peak = fabsf(s);
    if (fabsf(q) > sqr_peak) sqr_peak = fabsf(q);
  }

  for (int i = 0; i < PERIOD_SAMPLES; i++) {
    sawBL[i] = (int16_t)((saw_buf[i] / saw_peak) * AMP_FULLSCALE);
    sqrBL[i] = (int16_t)((sqr_buf[i] / sqr_peak) * AMP_FULLSCALE);
  }
}

void render_display() {
  int seg = current_seg;
  float t = (current_phase == PHASE_HOLD) ? 0.0f : (float)t_q_accum / 16777216.0f;
  if (t > 1.0f) t = 1.0f;
  const char* label = (t < 0.5f) ? shape_name(seg) : shape_name(seg + 1);

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setCursor(0, 7);
  u8g2.print(label);

  const int bar_x = 36;
  const int bar_w = W - bar_x;
  u8g2.drawFrame(bar_x, 1, bar_w, 7);
  float cycle_progress = (float)(total_samples % CYCLE_SAMPLES) / (float)CYCLE_SAMPLES;
  int fill = (int)(cycle_progress * (bar_w - 2) + 0.5f);
  if (fill > 0) u8g2.drawBox(bar_x + 1, 2, fill, 5);

  int prev_y = 0;
  for (int x = 0; x < W; x++) {
    float v = blend_display(seg, t, DISPLAY_K * (float)x + display_phase);
    int y = WAVE_MID - (int)(WAVE_AMP * v);
    if (y < WAVE_TOP)    y = WAVE_TOP;
    if (y > WAVE_BOTTOM) y = WAVE_BOTTOM;
    if (x == 0) prev_y = y;
    else {
      u8g2.drawLine(x - 1, prev_y, x, y);
      prev_y = y;
    }
  }

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);

  Wire.begin(5, 6);
  Wire.setClock(400000);
  u8g2.begin();

  build_bandlimited_tables();

  table_a = pick_a(current_seg);
  table_b = pick_b(current_seg);

  i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("I2S begin failed");
  } else {
    Serial.printf("I2S started: %d Hz, mono, tone=%d Hz\n", SAMPLE_RATE, TONE_HZ);
  }
}

void loop() {
  for (int i = 0; i < AUDIO_BUF_SAMPLES; i++) {
    int t_q = t_q_accum >> 16;
    if (t_q > 255) t_q = 255;
    int32_t blend = (int32_t)table_a[period_pos] * (256 - t_q)
                  + (int32_t)table_b[period_pos] * t_q;
    audioBuf[i] = (int16_t)(blend >> 8);
    if (++period_pos >= PERIOD_SAMPLES) period_pos = 0;

    if (MORPH_ENABLED) {
      if (current_phase == PHASE_GLIDE) {
        t_q_accum += T_Q_STEP;
      }
      if (--samples_left_in_phase == 0) {
        if (current_phase == PHASE_HOLD) {
          current_phase = PHASE_GLIDE;
          samples_left_in_phase = GLIDE_SAMPLES;
          t_q_accum = 0;
        } else {
          current_phase = PHASE_HOLD;
          samples_left_in_phase = HOLD_SAMPLES;
          current_seg = (current_seg + 1) % 3;
          table_a = pick_a(current_seg);
          table_b = pick_b(current_seg);
          t_q_accum = 0;
        }
      }
    }
  }

  i2s.write((uint8_t*)audioBuf, sizeof(audioBuf));
  total_samples += AUDIO_BUF_SAMPLES;

  display_phase += DISPLAY_PHASE_STEP;
  if (display_phase > TWO_PI_F) display_phase -= TWO_PI_F;

  static uint8_t buffer_count = 0;
  if (++buffer_count >= 2) {
    buffer_count = 0;
    render_display();
  }
}

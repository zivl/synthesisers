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
constexpr int AUDIO_BUF = 256;
int16_t audioBuf[AUDIO_BUF];

constexpr float BPM = 130.0f;
constexpr int STEPS = 16;
constexpr uint32_t SAMPLES_PER_STEP =
    (uint32_t)((double)SAMPLE_RATE * 60.0 / (BPM * 4.0));

uint32_t samples_into_step = 0;
int current_step = -1;

static inline uint32_t inc_for_hz(float hz) {
  return (uint32_t)((double)hz * 4294967296.0 / (double)SAMPLE_RATE);
}

constexpr float NOTE_F2  = 87.31f;
constexpr float NOTE_G2  = 98.00f;
constexpr float NOTE_GS2 = 103.83f;
constexpr float NOTE_A2  = 110.00f;
constexpr float NOTE_C3  = 130.81f;
constexpr float NOTE_E3  = 164.81f;
constexpr float NOTE_G3  = 196.00f;
constexpr float NOTE_A3  = 220.00f;

struct Step {
  float note;
  bool  accent;
  bool  slide;
};

const Step pattern[STEPS] = {
  {NOTE_A2, true,  false},
  {0,       false, false},
  {NOTE_A2, false, false},
  {NOTE_A3, false, true},
  {NOTE_A2, false, false},
  {0,       false, false},
  {NOTE_C3, true,  true},
  {NOTE_C3, false, false},
  {NOTE_A2, false, false},
  {0,       false, false},
  {NOTE_A2, false, false},
  {NOTE_G2, false, true},
  {NOTE_A2, true,  false},
  {0,       false, false},
  {NOTE_A2, false, false},
  {NOTE_GS2,false, false},
};

uint32_t osc_phase = 0;
uint32_t osc_inc = 0;
uint32_t osc_target_inc = 0;
int32_t  slide_remaining = 0;
int32_t  slide_step = 0;

int32_t amp_env_q16 = 0;
int32_t amp_env_decay_q16 = 0;

int32_t filt_env_q16 = 0;
int32_t filt_env_decay_q16 = 0;
int32_t filt_env_amount_f = 0;

int32_t svf_low = 0;
int32_t svf_band = 0;
int32_t filt_base_f = 0;

constexpr int32_t Q_RESO = 9000;

static int16_t saw_sample(uint32_t phase) {
  return (int16_t)((int32_t)phase >> 16);
}

static int decay_samples(int ms) {
  return SAMPLE_RATE * ms / 1000;
}

static int32_t hz_to_f_q16(float hz) {
  float f = 2.0f * sinf((float)PI * hz / (float)SAMPLE_RATE);
  return (int32_t)(f * 65536.0f);
}

static void trigger_note(int step) {
  const Step& st = pattern[step];
  if (st.note <= 0.0f) {
    return;
  }

  uint32_t new_inc = inc_for_hz(st.note);

  int prev = (step - 1 + STEPS) % STEPS;
  bool slide_in = pattern[prev].slide && pattern[prev].note > 0.0f;

  if (slide_in && osc_inc > 0) {
    slide_remaining = decay_samples(45);
    int32_t delta = (int32_t)new_inc - (int32_t)osc_inc;
    slide_step = delta / slide_remaining;
    osc_target_inc = new_inc;
  } else {
    osc_phase = 0;
    osc_inc = new_inc;
    osc_target_inc = new_inc;
    slide_remaining = 0;
  }

  int32_t amp_peak = st.accent ? 55000 : 32000;
  amp_env_q16 = amp_peak;
  amp_env_decay_q16 = amp_peak / decay_samples(slide_in ? 220 : 140);

  filt_env_q16 = 65536;
  filt_env_decay_q16 = 65536 / decay_samples(st.accent ? 380 : 220);
  filt_env_amount_f = st.accent ? hz_to_f_q16(3200.0f) : hz_to_f_q16(1700.0f);
}

static void render_display() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setCursor(0, 7);
  u8g2.print("303");
  u8g2.setCursor(24, 7);
  u8g2.print((int)BPM);

  for (int s = 0; s < STEPS; s++) {
    int x = s * 4 + 4;
    bool has_note = pattern[s].note > 0.0f;
    if (s == current_step) {
      u8g2.drawBox(x, 17, 3, 6);
    }
    if (has_note) {
      if (pattern[s].accent) u8g2.drawBox(x, 25, 3, 4);
      else                   u8g2.drawFrame(x, 25, 3, 4);
    }
    if (pattern[s].slide) {
      u8g2.drawHLine(x, 31, 3);
    }
  }

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);

  Wire.begin(5, 6);
  Wire.setClock(400000);
  u8g2.begin();

  filt_base_f = hz_to_f_q16(180.0f);

  i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("I2S begin failed");
  } else {
    Serial.printf("Acid303: %d BPM, base cutoff 180 Hz\n", (int)BPM);
  }
}

void loop() {
  for (int i = 0; i < AUDIO_BUF; i++) {
    if (samples_into_step == 0 || current_step < 0) {
      current_step = (current_step + 1) % STEPS;
      trigger_note(current_step);
    }
    samples_into_step++;
    if (samples_into_step >= SAMPLES_PER_STEP) samples_into_step = 0;

    if (slide_remaining > 0) {
      osc_inc = (uint32_t)((int32_t)osc_inc + slide_step);
      if (--slide_remaining == 0) osc_inc = osc_target_inc;
    }

    int16_t s = saw_sample(osc_phase);
    osc_phase += osc_inc;

    int32_t amp = amp_env_q16;
    if (amp < 0) amp = 0;
    int32_t voice_out = ((int32_t)s * amp) >> 16;
    amp_env_q16 -= amp_env_decay_q16;
    if (amp_env_q16 < 0) amp_env_q16 = 0;

    int32_t fe = filt_env_q16;
    if (fe < 0) fe = 0;
    int32_t f = filt_base_f + (int32_t)(((int64_t)fe * filt_env_amount_f) >> 16);
    if (f < 200)   f = 200;
    if (f > 56000) f = 56000;
    filt_env_q16 -= filt_env_decay_q16;
    if (filt_env_q16 < 0) filt_env_q16 = 0;

    int32_t old_band = svf_band;
    svf_low = svf_low + (int32_t)(((int64_t)f * old_band) >> 16);
    int32_t high = voice_out - svf_low - (int32_t)(((int64_t)Q_RESO * old_band) >> 16);
    svf_band = old_band + (int32_t)(((int64_t)f * high) >> 16);

    if (svf_low  >  (1 << 27)) svf_low  =  (1 << 27);
    if (svf_low  < -(1 << 27)) svf_low  = -(1 << 27);
    if (svf_band >  (1 << 27)) svf_band =  (1 << 27);
    if (svf_band < -(1 << 27)) svf_band = -(1 << 27);

    int32_t mix = (svf_low * 3) >> 2;
    if (mix > 32767)  mix = 32767;
    if (mix < -32768) mix = -32768;
    audioBuf[i] = (int16_t)mix;
  }

  i2s.write((uint8_t*)audioBuf, sizeof(audioBuf));

  static uint8_t buffer_count = 0;
  if (++buffer_count >= 4) {
    buffer_count = 0;
    render_display();
  }
}

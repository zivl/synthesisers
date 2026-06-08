#include <Wire.h>
#include <U8g2lib.h>
#include <ESP_I2S.h>
#include <math.h>

static inline uint32_t inc_for_hz(float hz);
static int16_t         saw_sample(uint32_t phase);
static int             decay_samples(int ms);
static int32_t         hz_to_f_q16(float hz);
static void            trigger_note(int step);
static void            read_pots();
static void            read_touch();
static void            draw_indicator(int x, int label_x, char ch, bool active);
static void            render_display();
void setup();
void loop();

U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
I2SClass i2s;

constexpr int I2S_BCLK = 7;
constexpr int I2S_LRC  = 10;
constexpr int I2S_DIN  = 20;

constexpr int POT_CUTOFF_PIN    = 0;
constexpr int POT_RESO_PIN      = 1;
constexpr int POT_ENVMOD_PIN    = 3;
constexpr int POT_DECAY_PIN     = 4;

constexpr int TOUCH_OCTAVE_PIN  = 21;  // active-HIGH, no A-pad bridge
constexpr int TOUCH_PATTERN_PIN = 2;   // active-LOW, A-pad bridged on module

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

constexpr float NOTE_E2  = 82.41f;
constexpr float NOTE_F2  = 87.31f;
constexpr float NOTE_G2  = 98.00f;
constexpr float NOTE_GS2 = 103.83f;
constexpr float NOTE_A2  = 110.00f;
constexpr float NOTE_B2  = 123.47f;
constexpr float NOTE_C3  = 130.81f;
constexpr float NOTE_E3  = 164.81f;
constexpr float NOTE_G3  = 196.00f;
constexpr float NOTE_A3  = 220.00f;

struct Step {
  float note;
  bool  accent;
  bool  slide;
};

// Pattern A — A minor acid groove (original)
const Step pattern_a[STEPS] = {
  {NOTE_A2,  true,  false},
  {0,        false, false},
  {NOTE_A2,  false, false},
  {NOTE_A3,  false, true },
  {NOTE_A2,  false, false},
  {0,        false, false},
  {NOTE_C3,  true,  true },
  {NOTE_C3,  false, false},
  {NOTE_A2,  false, false},
  {0,        false, false},
  {NOTE_A2,  false, false},
  {NOTE_G2,  false, true },
  {NOTE_A2,  true,  false},
  {0,        false, false},
  {NOTE_A2,  false, false},
  {NOTE_GS2, false, false},
};

// Pattern B — E minor/pentatonic variant
const Step pattern_b[STEPS] = {
  {NOTE_E3,  true,  false},
  {0,        false, false},
  {NOTE_E3,  false, false},
  {0,        false, false},
  {NOTE_A2,  false, true },
  {NOTE_E3,  true,  false},
  {0,        false, false},
  {NOTE_G2,  false, false},
  {0,        false, false},
  {NOTE_E3,  false, false},
  {0,        false, false},
  {NOTE_B2,  false, true },
  {NOTE_E3,  true,  false},
  {0,        false, false},
  {NOTE_G2,  false, false},
  {NOTE_E2,  false, false},
};

bool touch_octave  = false;
bool touch_pattern = false;
const Step* active_pattern = pattern_a;

// Pot change indicator
int      active_pot_idx       = -1;
uint32_t pot_display_until_ms = 0;

// MACD threshold: |fast_EMA - slow_EMA| needed to flag a pot as moving.
// fast (α=½) leads slow (α=⅛) by ~6× the per-call rate, so even slow
// deliberate turning produces >>20; ADC noise produces ≈10–13.
constexpr int      POT_CHANGE_THRESH = 20;
constexpr uint32_t POT_DISPLAY_MS   = 2000;
static const char* const POT_NAMES[] = {"CUT", "RES", "ENV", "DEC"};

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

int32_t q_reso = 9000;
int     filter_decay_ms = 220;
int32_t env_amount_normal_f = 33000;
int32_t env_amount_accent_f = 57632;

// Slow EMAs — used for audio parameters
uint16_t pot_cutoff = 2048;
uint16_t pot_reso   = 2048;
uint16_t pot_envmod = 2048;
uint16_t pot_decay  = 2048;

// Fast EMAs (α=½) — used only for MACD pot-change detection
uint16_t fast_cutoff = 2048;
uint16_t fast_reso   = 2048;
uint16_t fast_envmod = 2048;
uint16_t fast_decay  = 2048;

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
  const Step& st = active_pattern[step];
  if (st.note <= 0.0f) {
    return;
  }

  float note_hz = st.note * (touch_octave ? 2.0f : 1.0f);
  uint32_t new_inc = inc_for_hz(note_hz);

  int prev = (step - 1 + STEPS) % STEPS;
  bool slide_in = active_pattern[prev].slide && active_pattern[prev].note > 0.0f;

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
  int fd_ms = st.accent ? (filter_decay_ms * 17 / 10) : filter_decay_ms;
  if (fd_ms < 20) fd_ms = 20;
  filt_env_decay_q16 = 65536 / decay_samples(fd_ms);
  filt_env_amount_f = st.accent ? env_amount_accent_f : env_amount_normal_f;
}

static void read_pots() {
  // Sample each GPIO exactly once
  int raw[4] = {
    analogRead(POT_CUTOFF_PIN),
    analogRead(POT_RESO_PIN),
    analogRead(POT_ENVMOD_PIN),
    analogRead(POT_DECAY_PIN)
  };

  // Slow EMAs (α=⅛) — audio parameters
  pot_cutoff = (uint16_t)((pot_cutoff * 7u + (uint16_t)raw[0]) >> 3);
  pot_reso   = (uint16_t)((pot_reso   * 7u + (uint16_t)raw[1]) >> 3);
  pot_envmod = (uint16_t)((pot_envmod * 7u + (uint16_t)raw[2]) >> 3);
  pot_decay  = (uint16_t)((pot_decay  * 7u + (uint16_t)raw[3]) >> 3);

  // Fast EMAs (α=½) — lead the slow EMAs by ~6× the per-call rate when moving
  fast_cutoff = (uint16_t)((fast_cutoff + (uint16_t)raw[0]) >> 1);
  fast_reso   = (uint16_t)((fast_reso   + (uint16_t)raw[1]) >> 1);
  fast_envmod = (uint16_t)((fast_envmod + (uint16_t)raw[2]) >> 1);
  fast_decay  = (uint16_t)((fast_decay  + (uint16_t)raw[3]) >> 1);

  // MACD: |fast − slow| is large only when the pot is actually moving
  int diffs[4] = {
    abs((int)fast_cutoff - (int)pot_cutoff),
    abs((int)fast_reso   - (int)pot_reso),
    abs((int)fast_envmod - (int)pot_envmod),
    abs((int)fast_decay  - (int)pot_decay)
  };
  int best = -1, best_d = POT_CHANGE_THRESH - 1;
  for (int i = 0; i < 4; i++) {
    if (diffs[i] > best_d) { best_d = diffs[i]; best = i; }
  }
  if (best >= 0) {
    active_pot_idx       = best;
    pot_display_until_ms = millis() + POT_DISPLAY_MS;
  }

  uint32_t pc_sq = ((uint32_t)pot_cutoff * pot_cutoff) >> 12;
  float cutoff_hz = 60.0f + (pc_sq * (2500.0f - 60.0f) / 4095.0f);
  filt_base_f = hz_to_f_q16(cutoff_hz);

  q_reso = 28000 - ((int32_t)pot_reso * (28000 - 4000)) / 4095;

  int32_t emax = hz_to_f_q16(4000.0f);
  env_amount_normal_f = ((int32_t)pot_envmod * (emax >> 1)) / 4095;
  env_amount_accent_f = ((int32_t)pot_envmod * emax) / 4095;

  uint32_t pd_sq = ((uint32_t)pot_decay * pot_decay) >> 12;
  filter_decay_ms = 30 + (pd_sq * (1200 - 30)) / 4095;
}

static void read_touch() {
  touch_octave  = (digitalRead(TOUCH_OCTAVE_PIN) == HIGH);
  touch_pattern = (digitalRead(TOUCH_PATTERN_PIN) == LOW);  // A-pad bridged = active-LOW
}

// Draw an 8×8 indicator box. Filled+inverted text when active, outline+normal text when not.
static void draw_indicator(int x, int label_x, char ch, bool active) {
  if (active) {
    u8g2.drawBox(x, 0, 8, 8);
    u8g2.setDrawColor(0);
    u8g2.setCursor(label_x, 7);
    u8g2.print(ch);
    u8g2.setDrawColor(1);
  } else {
    u8g2.drawFrame(x, 0, 8, 8);
    u8g2.setCursor(label_x, 7);
    u8g2.print(ch);
  }
}

static void render_display() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);

  // Header: "303  130  [P][O]"
  // P = pattern (A/B), O = octave (^)
  u8g2.setCursor(0, 7);
  u8g2.print("303");
  u8g2.setCursor(20, 7);
  u8g2.print((int)BPM);

  // Pattern indicator box at x=44; octave indicator at x=55
  draw_indicator(44, 46, touch_pattern ? 'B' : 'A', touch_pattern);
  draw_indicator(55, 57, '^', touch_octave);

  // Step sequencer rows
  for (int s = 0; s < STEPS; s++) {
    int x = s * 4 + 4;
    bool has_note = active_pattern[s].note > 0.0f;
    if (s == current_step) {
      u8g2.drawBox(x, 17, 3, 6);
    }
    if (has_note) {
      if (active_pattern[s].accent) u8g2.drawBox(x, 25, 3, 4);
      else                          u8g2.drawFrame(x, 25, 3, 4);
    }
    if (active_pattern[s].slide) {
      u8g2.drawHLine(x, 31, 3);
    }
  }

  // Pot indicator: bottom 8 rows, only while timer is live
  if (millis() < pot_display_until_ms && active_pot_idx >= 0) {
    const uint16_t vals[4] = {pot_cutoff, pot_reso, pot_envmod, pot_decay};
    uint16_t val = vals[active_pot_idx];

    u8g2.setDrawColor(0);
    u8g2.drawBox(0, 32, 72, 8);
    u8g2.setDrawColor(1);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(0, 39);
    u8g2.print(POT_NAMES[active_pot_idx]);

    u8g2.drawFrame(21, 34, 50, 4);
    int fill = (int)((uint32_t)val * 50 / 4095);
    if (fill > 0) u8g2.drawBox(21, 34, fill, 4);
  }

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);

  pinMode(TOUCH_OCTAVE_PIN,  INPUT);
  pinMode(TOUCH_PATTERN_PIN, INPUT);

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
  read_pots();
  read_touch();
  active_pattern = touch_pattern ? pattern_b : pattern_a;


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
    int32_t high = voice_out - svf_low - (int32_t)(((int64_t)q_reso * old_band) >> 16);
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

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

constexpr int LUT_BITS = 10;
constexpr int LUT_SIZE = 1 << LUT_BITS;
constexpr int LUT_MASK = LUT_SIZE - 1;
int16_t sin_lut[LUT_SIZE];

constexpr float BPM = 118.0f;
constexpr int STEPS = 16;
constexpr uint32_t SAMPLES_PER_STEP =
    (uint32_t)((double)SAMPLE_RATE * 60.0 / (BPM * 4.0));

uint32_t samples_into_step = 0;
int current_step = -1;

static inline uint32_t inc_for_hz(float hz) {
  return (uint32_t)((double)hz * 4294967296.0 / (double)SAMPLE_RATE);
}

struct Voice;

constexpr float NOTE_A2 = 110.0f;
constexpr float NOTE_A3 = 220.0f;
constexpr float NOTE_C4 = 261.63f;
constexpr float NOTE_E4 = 329.63f;
constexpr float NOTE_G4 = 392.0f;
constexpr float NOTE_A4 = 440.0f;

const bool kick_pat[STEPS] = {
  1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0
};
const bool hat_pat[STEPS] = {
  0,0,1,0, 0,0,1,0, 0,0,1,0, 0,0,1,0
};
const float bass_pat[STEPS] = {
  NOTE_A2, 0, NOTE_A3, 0, NOTE_A2, 0, NOTE_A3, 0,
  NOTE_A2, 0, NOTE_A3, 0, NOTE_A2, 0, NOTE_A3, 0
};
const float lead_pat[STEPS] = {
  NOTE_A3, 0, 0, NOTE_E4, NOTE_A4, 0, NOTE_G4, 0,
  NOTE_E4, 0, NOTE_C4, 0, NOTE_A3, 0, 0, 0
};

struct Voice {
  uint32_t phase;
  uint32_t inc;
  int32_t env;
  int32_t env_decay;
  bool active;
};

Voice v_kick = {0};
Voice v_hat  = {0};
Voice v_bass = {0};
Voice v_lead = {0};

uint32_t kick_pitch_dec = 0;
uint32_t kick_min_inc = 0;
uint32_t hat_seed = 0x12345678u;

static void build_sin_lut() {
  for (int i = 0; i < LUT_SIZE; i++) {
    sin_lut[i] = (int16_t)(sinf(2.0f * (float)PI * i / LUT_SIZE) * 30000.0f);
  }
}

static int decay_samples_from_ms(float ms) {
  return (int)(SAMPLE_RATE * ms / 1000.0f);
}

static void trigger_kick() {
  v_kick.phase = 0;
  v_kick.inc = inc_for_hz(120.0f);
  v_kick.env = 22000;
  v_kick.env_decay = v_kick.env / decay_samples_from_ms(120.0f);
  v_kick.active = true;
}

static void trigger_hat() {
  v_hat.env = 6000;
  v_hat.env_decay = v_hat.env / decay_samples_from_ms(28.0f);
  v_hat.active = true;
}

static void trigger_bass(float hz) {
  v_bass.phase = 0;
  v_bass.inc = inc_for_hz(hz);
  v_bass.env = 14000;
  v_bass.env_decay = v_bass.env / decay_samples_from_ms(180.0f);
  v_bass.active = true;
}

static void trigger_lead(float hz) {
  v_lead.phase = 0;
  v_lead.inc = inc_for_hz(hz);
  v_lead.env = 10000;
  v_lead.env_decay = v_lead.env / decay_samples_from_ms(220.0f);
  v_lead.active = true;
}

static int16_t process_kick() {
  if (!v_kick.active) return 0;
  int16_t s = sin_lut[(v_kick.phase >> 22) & LUT_MASK];
  int32_t out = ((int32_t)s * v_kick.env) >> 16;
  v_kick.phase += v_kick.inc;
  if (v_kick.inc > kick_min_inc + kick_pitch_dec) v_kick.inc -= kick_pitch_dec;
  v_kick.env -= v_kick.env_decay;
  if (v_kick.env <= 0) { v_kick.env = 0; v_kick.active = false; }
  return (int16_t)out;
}

static int16_t process_hat() {
  if (!v_hat.active) return 0;
  hat_seed = hat_seed * 1103515245u + 12345u;
  int16_t noise = (int16_t)(hat_seed >> 16);
  int32_t out = ((int32_t)noise * v_hat.env) >> 16;
  v_hat.env -= v_hat.env_decay;
  if (v_hat.env <= 0) { v_hat.env = 0; v_hat.active = false; }
  return (int16_t)out;
}

static int16_t process_bass() {
  if (!v_bass.active) return 0;
  int16_t s = sin_lut[(v_bass.phase >> 22) & LUT_MASK];
  int32_t out = ((int32_t)s * v_bass.env) >> 16;
  v_bass.phase += v_bass.inc;
  v_bass.env -= v_bass.env_decay;
  if (v_bass.env <= 0) { v_bass.env = 0; v_bass.active = false; }
  return (int16_t)out;
}

static int16_t process_lead() {
  if (!v_lead.active) return 0;
  int16_t s = sin_lut[(v_lead.phase >> 22) & LUT_MASK];
  int32_t out = ((int32_t)s * v_lead.env) >> 16;
  v_lead.phase += v_lead.inc;
  v_lead.env -= v_lead.env_decay;
  if (v_lead.env <= 0) { v_lead.env = 0; v_lead.active = false; }
  return (int16_t)out;
}

static void on_step(int step) {
  if (kick_pat[step]) trigger_kick();
  if (hat_pat[step])  trigger_hat();
  if (bass_pat[step] > 0.0f) trigger_bass(bass_pat[step]);
  if (lead_pat[step] > 0.0f) trigger_lead(lead_pat[step]);
}

static void render_display() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setCursor(0, 7);
  u8g2.print("DISCO");
  u8g2.setCursor(40, 7);
  u8g2.print((int)BPM);

  int active_beat = (current_step >= 0) ? (current_step / 4) : -1;
  for (int b = 0; b < 4; b++) {
    int x = 9 + b * 18;
    if (b == active_beat) u8g2.drawDisc(x, 19, 3);
    else                  u8g2.drawCircle(x, 19, 3);
  }

  for (int s = 0; s < STEPS; s++) {
    int x = s * 4 + 4;
    if (s == current_step) u8g2.drawBox(x, 31, 3, 8);
    else                   u8g2.drawFrame(x, 31, 3, 8);
  }

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);

  Wire.begin(5, 6);
  Wire.setClock(400000);
  u8g2.begin();

  build_sin_lut();

  uint32_t kick_start_inc = inc_for_hz(120.0f);
  kick_min_inc = inc_for_hz(40.0f);
  int sweep_samples = decay_samples_from_ms(80.0f);
  kick_pitch_dec = (kick_start_inc - kick_min_inc) / sweep_samples;

  i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("I2S begin failed");
  } else {
    Serial.printf("Disco80: %d Hz, %d BPM, %d samples/step\n",
                  SAMPLE_RATE, (int)BPM, (int)SAMPLES_PER_STEP);
  }
}

void loop() {
  for (int i = 0; i < AUDIO_BUF; i++) {
    if (samples_into_step == 0 || current_step < 0) {
      current_step = (current_step + 1) % STEPS;
      on_step(current_step);
    }
    samples_into_step++;
    if (samples_into_step >= SAMPLES_PER_STEP) samples_into_step = 0;

    int32_t mix = process_kick() + process_hat()
                + process_bass() + process_lead();
    mix = mix * 6;
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

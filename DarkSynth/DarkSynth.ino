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

constexpr float BPM = 66.0f;
constexpr int STEPS = 32;
constexpr uint32_t SAMPLES_PER_STEP =
    (uint32_t)((double)SAMPLE_RATE * 60.0 / (BPM * 4.0));

uint32_t samples_into_step = 0;
int current_step = -1;

static inline uint32_t inc_for_hz(float hz) {
  return (uint32_t)((double)hz * 4294967296.0 / (double)SAMPLE_RATE);
}

constexpr float NOTE_D2 = 73.42f;
constexpr float NOTE_F2 = 87.31f;
constexpr float NOTE_A2 = 110.00f;
constexpr float NOTE_D3 = 146.83f;
constexpr float NOTE_F3 = 174.61f;
constexpr float NOTE_A3 = 220.00f;
constexpr float NOTE_C4 = 261.63f;
constexpr float NOTE_D4 = 293.66f;

const float bass_pat[STEPS] = {
  NOTE_D2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  NOTE_D2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
const float sub_pat[STEPS] = {
  0, 0, 0, 0, 0, 0, 0, 0, NOTE_A2, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, NOTE_F2, 0, 0, 0, 0, 0, 0, 0
};
const float lead_pat[STEPS] = {
  NOTE_D4, 0, 0, 0, NOTE_C4, 0, 0, 0, NOTE_A3, 0, 0, 0, NOTE_F3, 0, 0, 0,
  NOTE_D3, 0, 0, 0, NOTE_F3, 0, 0, 0, NOTE_A3, 0, 0, 0, NOTE_C4, 0, 0, 0
};

uint32_t v_bass_phase = 0, v_bass_inc = 0;
int32_t  v_bass_env_q8 = 0, v_bass_decay_q8 = 0;
bool     v_bass_active = false;

uint32_t v_sub_phase = 0, v_sub_inc = 0;
int32_t  v_sub_env_q8 = 0, v_sub_decay_q8 = 0;
bool     v_sub_active = false;

uint32_t v_lead_phase = 0, v_lead_inc = 0;
int32_t  v_lead_env_q8 = 0, v_lead_decay_q8 = 0;
bool     v_lead_active = false;

constexpr int LPF_COEFF = 90;
int32_t lpf_state = 0;

static int decay_samples_from_ms(int ms) {
  return SAMPLE_RATE * ms / 1000;
}

static int16_t saw_sample(uint32_t phase) {
  return (int16_t)((int32_t)phase >> 16);
}

static void trigger_bass(float hz) {
  v_bass_phase = 0;
  v_bass_inc = inc_for_hz(hz);
  v_bass_env_q8 = 22000 << 8;
  v_bass_decay_q8 = v_bass_env_q8 / decay_samples_from_ms(8000);
  v_bass_active = true;
}

static void trigger_sub(float hz) {
  v_sub_phase = 0;
  v_sub_inc = inc_for_hz(hz);
  v_sub_env_q8 = 14000 << 8;
  v_sub_decay_q8 = v_sub_env_q8 / decay_samples_from_ms(6000);
  v_sub_active = true;
}

static void trigger_lead(float hz) {
  v_lead_phase = 0;
  v_lead_inc = inc_for_hz(hz);
  v_lead_env_q8 = 16000 << 8;
  v_lead_decay_q8 = v_lead_env_q8 / decay_samples_from_ms(1400);
  v_lead_active = true;
}

static int16_t process_bass() {
  if (!v_bass_active) return 0;
  int16_t s = saw_sample(v_bass_phase);
  int32_t env = v_bass_env_q8 >> 8;
  int32_t out = ((int32_t)s * env) >> 16;
  v_bass_phase += v_bass_inc;
  v_bass_env_q8 -= v_bass_decay_q8;
  if (v_bass_env_q8 <= 0) { v_bass_env_q8 = 0; v_bass_active = false; }
  return (int16_t)out;
}

static int16_t process_sub() {
  if (!v_sub_active) return 0;
  int16_t s = saw_sample(v_sub_phase);
  int32_t env = v_sub_env_q8 >> 8;
  int32_t out = ((int32_t)s * env) >> 16;
  v_sub_phase += v_sub_inc;
  v_sub_env_q8 -= v_sub_decay_q8;
  if (v_sub_env_q8 <= 0) { v_sub_env_q8 = 0; v_sub_active = false; }
  return (int16_t)out;
}

static int16_t process_lead() {
  if (!v_lead_active) return 0;
  int16_t s = saw_sample(v_lead_phase);
  int32_t env = v_lead_env_q8 >> 8;
  int32_t out = ((int32_t)s * env) >> 16;
  v_lead_phase += v_lead_inc;
  v_lead_env_q8 -= v_lead_decay_q8;
  if (v_lead_env_q8 <= 0) { v_lead_env_q8 = 0; v_lead_active = false; }
  return (int16_t)out;
}

static void on_step(int step) {
  if (bass_pat[step] > 0.0f) trigger_bass(bass_pat[step]);
  if (sub_pat[step]  > 0.0f) trigger_sub(sub_pat[step]);
  if (lead_pat[step] > 0.0f) trigger_lead(lead_pat[step]);
}

static void render_display() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setCursor(0, 7);
  u8g2.print("DARK");
  u8g2.setCursor(38, 7);
  u8g2.print((int)BPM);

  u8g2.drawFrame(0, 30, 71, 8);
  int prog = (current_step >= 0) ? ((current_step + 1) * 69) / STEPS : 0;
  if (prog > 0) u8g2.drawBox(1, 31, prog, 6);

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);

  Wire.begin(5, 6);
  Wire.setClock(400000);
  u8g2.begin();

  i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("I2S begin failed");
  } else {
    Serial.printf("DarkSynth: %d Hz, %d BPM, looping\n", SAMPLE_RATE, (int)BPM);
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

    int32_t mix = process_bass() + process_sub() + process_lead();
    mix = (mix * 3) >> 1;

    int32_t diff = mix - lpf_state;
    lpf_state += (diff * LPF_COEFF) >> 8;
    int32_t out = lpf_state;

    if (out > 32767)  out = 32767;
    if (out < -32768) out = -32768;
    audioBuf[i] = (int16_t)out;
  }

  i2s.write((uint8_t*)audioBuf, sizeof(audioBuf));

  static uint8_t buffer_count = 0;
  if (++buffer_count >= 4) {
    buffer_count = 0;
    render_display();
  }
}

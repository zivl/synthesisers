// GPIO pin connected to LM386 input (via 10k resistor to pin 3, pin 2 to GND)
#define TONE_PIN 25

// F#4 = 369.99 Hz
#define FSHARP4_NOTE NOTE_Fs
#define FSHARP4_OCTAVE 4

#define TONE_DURATION_MS  200
#define PERIOD_MS        5000

void setup() {
  Serial.begin(115200);
  ledcAttach(TONE_PIN, 370, 8);
  Serial.println("F#4 beeping every 2 seconds");
}

void loop() {
  ledcWriteNote(TONE_PIN, FSHARP4_NOTE, FSHARP4_OCTAVE);
  delay(TONE_DURATION_MS);
  ledcWrite(TONE_PIN, 0);  // silence
  delay(PERIOD_MS - TONE_DURATION_MS);
}

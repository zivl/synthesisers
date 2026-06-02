#ifndef LED_BUILTIN
#define LED_BUILTIN 8
#endif

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("on");
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("off");
  delay(500);
}

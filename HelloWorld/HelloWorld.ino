#include <TFT_eSPI.h>

TFT_eSPI tft;

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);  // landscape, USB on left
  tft.fillScreen(TFT_BLACK);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(4);
  tft.drawString("Hello World", 160, 120);

  Serial.println("Hello World displayed.");
}

void loop() {}

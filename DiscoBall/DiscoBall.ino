// Disco Ball v2 — CYD (ESP32-2432S028R)
// Flicker-free via two 320×120 sprites (75 KB each).
// WiFi status shown on RGB LED (green=connected, red=disconnected) and
// as a coloured dot in the top-right corner of the screen.

#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <WiFi.h>

// ── WiFi credentials ──────────────────────────────────────────────────────────
#define WIFI_SSID  "Ziv's iPhone (2)"
#define WIFI_PASS  "0036574226"

// ── RGB LED (active-low) ──────────────────────────────────────────────────────
#define LED_R     4
#define LED_G    16
#define LED_B    17
#define LEDC_FREQ 5000
#define LEDC_RES  12
#define MAX_DUTY  4095

// ── Touch HSPI ────────────────────────────────────────────────────────────────
#define TOUCH_CS_PIN  33
#define TOUCH_IRQ     36
#define TOUCH_CLK     25
#define TOUCH_MISO    39
#define TOUCH_MOSI    32

TFT_eSPI    tft;
TFT_eSprite canvas = TFT_eSprite(&tft);
SPIClass    touchSPI(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ);

// ── Disco ball ────────────────────────────────────────────────────────────────
#define BALL_CX  160
#define BALL_CY   78
#define BALL_R    50
#define TILE_W     8
#define TILE_H     7

// ── Beams ─────────────────────────────────────────────────────────────────────
#define N_BEAMS  10

// ── Sparkles ──────────────────────────────────────────────────────────────────
#define N_SPARKLES 40
struct Sparkle { int16_t x, y; uint8_t ttl, maxTtl; uint16_t col; };
Sparkle sparks[N_SPARKLES];

// ── Animation state ───────────────────────────────────────────────────────────
float    rotation  = 0.0f;
float    globalHue = 0.0f;

// ── Colour helpers ────────────────────────────────────────────────────────────
uint16_t hsv565(float h, float s = 1.0f, float v = 1.0f) {
  h = fmodf(h, 360.0f);
  if (h < 0) h += 360.0f;
  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;
  float r, g, b;
  if      (h < 60)  { r=c; g=x; b=0; }
  else if (h < 120) { r=x; g=c; b=0; }
  else if (h < 180) { r=0; g=c; b=x; }
  else if (h < 240) { r=0; g=x; b=c; }
  else if (h < 300) { r=x; g=0; b=c; }
  else              { r=c; g=0; b=x; }
  return tft.color565((uint8_t)((r+m)*255), (uint8_t)((g+m)*255), (uint8_t)((b+m)*255));
}

// Blend two RGB565 colours by factor t (0=a, 1=b)
uint16_t blend565(uint16_t a, uint16_t b, float t) {
  uint8_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  uint8_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  return (uint16_t)(ar + t*(br-ar)) << 11 |
         (uint16_t)(ag + t*(bg-ag)) << 5  |
         (uint16_t)(ab + t*(bb-ab));
}

// ── Touch calibration (CYD resistive panel) ───────────────────────────────────
#define TS_MINX 200
#define TS_MAXX 3900
#define TS_MINY 200
#define TS_MAXY 3900

// ── WiFi state ────────────────────────────────────────────────────────────────
enum WifiState { WIFI_IDLE, WIFI_CONNECTING, WIFI_CONNECTED };
WifiState wifiState     = WIFI_IDLE;
bool      wifiConnected = false;

// ── Scan state ────────────────────────────────────────────────────────────────
enum ScanState { SCAN_IDLE, SCAN_RUNNING, SCAN_RESULTS };
ScanState scanState   = SCAN_IDLE;
int       scanCount   = 0;   // networks found

// Scan button (bottom-left, screen coords)
#define SCAN_BTN_X   5
#define SCAN_BTN_Y  217
#define SCAN_BTN_W   60
#define SCAN_BTN_H   20

void setLedColor(uint8_t r, uint8_t g, uint8_t b) {
  // Active-low: invert each channel
  ledcWrite(LED_R, MAX_DUTY - (uint32_t)(r * MAX_DUTY / 255));
  ledcWrite(LED_G, MAX_DUTY - (uint32_t)(g * MAX_DUTY / 255));
  ledcWrite(LED_B, MAX_DUTY - (uint32_t)(b * MAX_DUTY / 255));
}

void updateWifiStatus() {
  wl_status_t s = WiFi.status();
  if (s == WL_CONNECTED) {
    if (wifiState != WIFI_CONNECTED) {
      wifiState     = WIFI_CONNECTED;
      wifiConnected = true;
      Serial.printf("WiFi connected — IP: %s\n", WiFi.localIP().toString().c_str());
    }
  } else if (wifiState == WIFI_CONNECTING) {
    if (s == WL_CONNECT_FAILED || s == WL_NO_SSID_AVAIL) {
      wifiState     = WIFI_IDLE;
      wifiConnected = false;
      Serial.println("WiFi connection failed — tap Connect to retry");
    }
  }
  if (wifiConnected) setLedColor(0, 255, 0);
  else               setLedColor(255, 0, 0);
}

void startWifiConnect() {
  if (wifiState == WIFI_CONNECTING || wifiState == WIFI_CONNECTED) return;
  wifiState = WIFI_CONNECTING;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to %s ...\n", WIFI_SSID);
}

// ── WiFi panel (top-right corner) ────────────────────────────────────────────
// Layout (screen coords): dot at (308,11), button at (240,3)–(299,23)
#define WIFI_DOT_X   308
#define WIFI_DOT_Y    11
#define WIFI_BTN_X   238   // button left edge
#define WIFI_BTN_Y     3   // button top edge
#define WIFI_BTN_W    62
#define WIFI_BTN_H    20

void drawWifiDot(int yOff) {
  // ── Status dot ──────────────────────────────────────────────────
  int dy = WIFI_DOT_Y - yOff;
  if (dy >= -8 && dy < 128) {
    uint16_t col = wifiConnected ? tft.color565(0, 230, 80)
                                 : tft.color565(255, 40, 40);
    canvas.drawCircle(WIFI_DOT_X, dy, 8, blend565(col, TFT_BLACK, 0.55f));
    canvas.fillCircle(WIFI_DOT_X, dy, 6, col);
    canvas.fillCircle(WIFI_DOT_X - 2, dy - 2, 1, TFT_WHITE);
  }

  // ── Button (hidden when connected) ──────────────────────────────
  int by = WIFI_BTN_Y - yOff;
  if (by < -WIFI_BTN_H || by > 124) return;

  if (wifiState == WIFI_CONNECTED) {
    // Just a small "Connected" label
    canvas.setTextColor(tft.color565(80, 220, 120), 0x0008);
    canvas.setTextDatum(MR_DATUM);
    canvas.setTextSize(1);
    canvas.drawString("Connected", WIFI_DOT_X - 11, dy);
  } else if (wifiState == WIFI_CONNECTING) {
    // Animated dots
    static uint8_t dotFrame = 0;
    dotFrame = (dotFrame + 1) % 6;
    const char* dots[] = {"   ", ".  ", ".. ", "...", " ..", "  ."};
    canvas.fillRoundRect(WIFI_BTN_X, by, WIFI_BTN_W, WIFI_BTN_H, 4,
                         tft.color565(40, 40, 60));
    canvas.drawRoundRect(WIFI_BTN_X, by, WIFI_BTN_W, WIFI_BTN_H, 4,
                         tft.color565(80, 80, 120));
    canvas.setTextColor(tft.color565(160, 160, 200), tft.color565(40, 40, 60));
    canvas.setTextDatum(MC_DATUM);
    canvas.setTextSize(1);
    canvas.drawString(dots[dotFrame], WIFI_BTN_X + WIFI_BTN_W/2, by + WIFI_BTN_H/2);
  } else {
    // "Connect" button
    canvas.fillRoundRect(WIFI_BTN_X, by, WIFI_BTN_W, WIFI_BTN_H, 4,
                         tft.color565(20, 60, 160));
    canvas.drawRoundRect(WIFI_BTN_X, by, WIFI_BTN_W, WIFI_BTN_H, 4,
                         tft.color565(80, 130, 255));
    canvas.setTextColor(TFT_WHITE, tft.color565(20, 60, 160));
    canvas.setTextDatum(MC_DATUM);
    canvas.setTextSize(1);
    canvas.drawString("Connect", WIFI_BTN_X + WIFI_BTN_W/2, by + WIFI_BTN_H/2);
  }
}

// ── Scan button (bottom-left) ─────────────────────────────────────────────────
void drawScanButton(int yOff) {
  int by = SCAN_BTN_Y - yOff;
  if (by < -SCAN_BTN_H || by > 124) return;

  if (scanState == SCAN_RUNNING) {
    static uint8_t sf = 0; sf = (sf + 1) % 6;
    const char* dots[] = {"   ", ".  ", ".. ", "...", " ..", "  ."};
    canvas.fillRoundRect(SCAN_BTN_X, by, SCAN_BTN_W, SCAN_BTN_H, 4,
                         tft.color565(30, 30, 50));
    canvas.drawRoundRect(SCAN_BTN_X, by, SCAN_BTN_W, SCAN_BTN_H, 4,
                         tft.color565(80, 80, 130));
    canvas.setTextColor(tft.color565(160, 160, 200), tft.color565(30, 30, 50));
    canvas.setTextDatum(MC_DATUM); canvas.setTextSize(1);
    canvas.drawString(dots[sf], SCAN_BTN_X + SCAN_BTN_W/2, by + SCAN_BTN_H/2);
  } else {
    canvas.fillRoundRect(SCAN_BTN_X, by, SCAN_BTN_W, SCAN_BTN_H, 4,
                         tft.color565(20, 50, 80));
    canvas.drawRoundRect(SCAN_BTN_X, by, SCAN_BTN_W, SCAN_BTN_H, 4,
                         tft.color565(60, 140, 220));
    canvas.setTextColor(TFT_WHITE, tft.color565(20, 50, 80));
    canvas.setTextDatum(MC_DATUM); canvas.setTextSize(1);
    canvas.drawString("Scan WiFi", SCAN_BTN_X + SCAN_BTN_W/2, by + SCAN_BTN_H/2);
  }
}

// ── Scan results overlay (full screen via canvas, two passes) ─────────────────
// Draw a signal-strength icon (4 bars) at (x, y)
void drawSignalBars(int x, int y, int rssi) {
  int bars = (rssi > -55) ? 4 : (rssi > -65) ? 3 : (rssi > -75) ? 2 : 1;
  uint16_t on  = tft.color565(80, 220, 100);
  uint16_t off = tft.color565(50,  50,  50);
  for (int b = 0; b < 4; b++) {
    int bh = 3 + b * 2;
    uint16_t c = (b < bars) ? on : off;
    canvas.fillRect(x + b * 5, y + (8 - bh), 4, bh, c);
  }
}

void drawScanResults(int yOff) {
  uint16_t bgCol  = tft.color565(8, 4, 20);
  uint16_t hdrCol = tft.color565(180, 100, 255);
  uint16_t dimCol = tft.color565(100, 100, 130);

  canvas.fillSprite(bgCol);

  // Header (top half only)
  if (yOff == 0) {
    canvas.setTextColor(hdrCol, bgCol);
    canvas.setTextDatum(TL_DATUM);
    canvas.setTextSize(1);
    canvas.drawString("WiFi Networks", 8, 6, 2);
    char buf[24];
    snprintf(buf, sizeof(buf), "%d found   tap to close", scanCount);
    canvas.setTextColor(dimCol, bgCol);
    canvas.drawString(buf, 8, 24, 1);
    canvas.drawFastHLine(0, 34, 320, tft.color565(50, 30, 80));
  }

  // Each network row is 22px tall; first row starts at screen Y=38
  const int ROW_H = 22, LIST_Y = 38;
  int maxRows = min(scanCount, 9);

  for (int i = 0; i < maxRows; i++) {
    int rowY = LIST_Y + i * ROW_H;          // screen Y
    int ry   = rowY - yOff;                 // sprite Y
    if (ry + ROW_H < 0 || ry > 120) continue;

    // Alternating row tint
    uint16_t rowBg = (i & 1) ? tft.color565(12, 6, 28) : bgCol;
    canvas.fillRect(0, ry, 320, ROW_H, rowBg);

    // Signal bars
    drawSignalBars(6, ry + 5, WiFi.RSSI(i));

    // Lock icon (padlock ascii ≈ secured)
    bool secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    canvas.setTextColor(secured ? tft.color565(255, 200, 50) : dimCol, rowBg);
    canvas.setTextDatum(ML_DATUM);
    canvas.setTextSize(1);
    canvas.drawString(secured ? "a" : " ", 30, ry + ROW_H/2);

    // SSID
    canvas.setTextColor(TFT_WHITE, rowBg);
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) ssid = "(hidden)";
    canvas.drawString(ssid.c_str(), 40, ry + ROW_H/2);

    // RSSI value
    char rssiStr[10];
    snprintf(rssiStr, sizeof(rssiStr), "%d dBm", WiFi.RSSI(i));
    canvas.setTextColor(dimCol, rowBg);
    canvas.setTextDatum(MR_DATUM);
    canvas.drawString(rssiStr, 314, ry + ROW_H/2);
  }

  canvas.pushSprite(0, yOff);
}

// ── Spotlight cones ───────────────────────────────────────────────────────────
void drawBeams(int yOff) {
  uint16_t bg = tft.color565(5, 0, 12);

  for (int i = 0; i < N_BEAMS; i++) {
    float angle = rotation + i * (2.0f * M_PI / N_BEAMS);
    float bh    = fmodf(globalHue + i * (360.0f / N_BEAMS), 360.0f);
    float dx = cosf(angle), dy = sinf(angle);

    float t = 1e9f;
    if (dx >  0.001f) t = min(t, (319.0f - BALL_CX) / dx);
    if (dx < -0.001f) t = min(t, (0.0f   - BALL_CX) / dx);
    if (dy >  0.001f) t = min(t, (239.0f - BALL_CY) / dy);
    if (dy < -0.001f) t = min(t, (0.0f   - BALL_CY) / dy);

    int ex = BALL_CX + (int)(dx * t);
    int ey = (BALL_CY + (int)(dy * t)) - yOff;
    int bcy = BALL_CY - yOff;

    uint16_t c1 = hsv565(bh, 1.0f, 0.95f);
    uint16_t c2 = hsv565(bh, 0.7f, 0.45f);
    uint16_t c3 = hsv565(bh, 0.4f, 0.18f);

    // Spotlight cone: apex at ball centre, spreading out to edge
    float px = -dy, py = dx;
    // Outer cone (widest, dimmest)
    canvas.fillTriangle(BALL_CX, bcy,
                        ex + (int)(px*18), ey + (int)(py*18),
                        ex - (int)(px*18), ey - (int)(py*18), c3);
    // Middle cone
    canvas.fillTriangle(BALL_CX, bcy,
                        ex + (int)(px*9),  ey + (int)(py*9),
                        ex - (int)(px*9),  ey - (int)(py*9),  c2);
    // Inner bright core
    canvas.fillTriangle(BALL_CX, bcy,
                        ex + (int)(px*3),  ey + (int)(py*3),
                        ex - (int)(px*3),  ey - (int)(py*3),  c1);

    // Radial glow where cone hits the wall/floor
    for (int gr = 18; gr >= 2; gr -= 2) {
      float gv = 1.0f - (float)gr / 18.0f;
      canvas.drawCircle(ex, ey, gr, hsv565(bh, 0.8f + gv*0.2f, gv));
    }
    canvas.fillCircle(ex, ey, 3, c1);
  }
}

// ── 3D disco ball ─────────────────────────────────────────────────────────────
// Light direction for sphere shading (upper-left, normalised)
static const float LX = -0.45f, LY = -0.45f, LZ = 0.775f;

void drawDiscoBall(int yOff) {
  int bcy = BALL_CY - yOff;

  // Base: dark sphere
  canvas.fillCircle(BALL_CX, bcy, BALL_R, tft.color565(18, 18, 18));

  int cols = (2 * BALL_R) / TILE_W + 2;
  int rows = (2 * BALL_R) / TILE_H + 2;
  int rSq  = (BALL_R - 2) * (BALL_R - 2);

  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      // Tile centre in sphere-local coords
      float cx = (BALL_CX - BALL_R + col * TILE_W + TILE_W/2.0f) - BALL_CX;
      float cy = (BALL_CY - BALL_R + row * TILE_H + TILE_H/2.0f) - BALL_CY;

      float dist2 = cx*cx + cy*cy;
      if (dist2 >= rSq) continue;

      // Surface normal (sphere projection)
      float nz  = sqrtf((float)rSq - dist2) / (BALL_R - 2);
      float nx  = cx / (BALL_R - 2);
      float ny  = cy / (BALL_R - 2);

      // Diffuse shading from fixed light
      float diffuse = max(0.0f, nx*LX + ny*LY + nz*LZ);

      // Specular highlight (Blinn-Phong, half-vector ≈ light direction when viewer is far)
      float spec = powf(max(0.0f, nz*LZ + nx*LX + ny*LY), 12.0f);

      // Tile colour: hue driven by globalHue + position offset
      float tileHue = fmodf(globalHue + col * 22.0f + row * 35.0f, 360.0f);

      // Shimmer: each tile "switches on" when a rotating wave passes over it
      float wave = (sinf(col * 1.1f + rotation * 2.8f)
                  * sinf(row * 0.9f - rotation * 2.2f) + 1.0f) * 0.5f;

      uint16_t tc;
      if (wave > 0.58f) {
        float v = diffuse * 0.5f + wave * 0.5f;
        v = min(1.0f, v + spec * 0.6f);
        tc = hsv565(tileHue, 0.85f, v);
      } else if (wave > 0.32f) {
        // Dim grey tile — still shaded by diffuse
        uint8_t g = (uint8_t)(diffuse * 45 + 8);
        tc = tft.color565(g, g, g);
      } else {
        tc = TFT_BLACK;
      }

      int tx = BALL_CX - BALL_R + col * TILE_W;
      int ty = bcy - BALL_R + row * TILE_H;
      canvas.fillRect(tx + 1, ty + 1, TILE_W - 2, TILE_H - 2, tc);
    }
  }

  // Chrome equator ring
  canvas.drawCircle(BALL_CX, bcy, BALL_R,     TFT_WHITE);
  canvas.drawCircle(BALL_CX, bcy, BALL_R - 1, tft.color565(100, 100, 120));
  canvas.drawCircle(BALL_CX, bcy, BALL_R - 2, tft.color565(40,  40,  50));

  // Specular highlight (sharp white spot, upper-left)
  int hx = BALL_CX - (int)(BALL_R * 0.32f);
  int hy = bcy     - (int)(BALL_R * 0.32f);
  canvas.fillCircle(hx,   hy,   8, tft.color565(180, 190, 255));
  canvas.fillCircle(hx,   hy,   5, tft.color565(220, 230, 255));
  canvas.fillCircle(hx,   hy,   2, TFT_WHITE);
}

// ── Star sparkles ─────────────────────────────────────────────────────────────
void drawStar(int x, int y, int sz, uint16_t col) {
  canvas.drawLine(x - sz, y,      x + sz, y,      col);
  canvas.drawLine(x,      y - sz, x,      y + sz, col);
  int d = sz * 7 / 10;
  canvas.drawLine(x - d, y - d, x + d, y + d, blend565(col, TFT_BLACK, 0.45f));
  canvas.drawLine(x + d, y - d, x - d, y + d, blend565(col, TFT_BLACK, 0.45f));
}

void spawnSparkle() {
  for (int i = 0; i < N_SPARKLES; i++) {
    if (sparks[i].ttl != 0) continue;
    int16_t sx, sy; int tries = 0;
    do {
      sx = random(6, 314); sy = random(6, 234); tries++;
    } while (tries < 10 &&
             (sx-BALL_CX)*(sx-BALL_CX) + (sy-BALL_CY)*(sy-BALL_CY)
             < (BALL_R + 14) * (BALL_R + 14));
    uint8_t life = random(8, 24);
    sparks[i] = { sx, sy, life, life, hsv565((float)random(0, 360)) };
    break;
  }
}

void drawSparkles(int yOff) {
  for (int i = 0; i < N_SPARKLES; i++) {
    if (sparks[i].ttl == 0) continue;
    float life = (float)sparks[i].ttl / sparks[i].maxTtl;
    int sz = (life > 0.5f) ? (int)(life * 5) + 1 : (int)(life * 4) + 1;
    sz = max(1, min(sz, 5));
    drawStar(sparks[i].x, sparks[i].y - yOff, sz, sparks[i].col);
    sparks[i].ttl--;
  }
}

// ── Background ────────────────────────────────────────────────────────────────
static const uint16_t BG      = 0x0008;  // near-black with tiny blue tint
static const uint16_t FLOOR_Y = 200;     // screen Y of floor line

void drawBackground(int yOff) {
  canvas.fillSprite(BG);

  // Subtle floor line
  int fy = FLOOR_Y - yOff;
  if (fy >= 0 && fy < 120) {
    canvas.drawFastHLine(0, fy, 320, tft.color565(18, 10, 30));
    canvas.drawFastHLine(0, fy + 1, 320, tft.color565(10, 5, 18));
  }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // RGB LED
  ledcAttach(LED_R, LEDC_FREQ, LEDC_RES);
  ledcAttach(LED_G, LEDC_FREQ, LEDC_RES);
  ledcAttach(LED_B, LEDC_FREQ, LEDC_RES);
  setLedColor(255, 0, 0);  // start red (disconnected)

  void* buf = canvas.createSprite(320, 120);
  if (!buf) {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.drawString("SPRITE ALLOC FAILED", 10, 110, 2);
    Serial.println("ERROR: sprite alloc failed");
    while (true) delay(1000);
  }
  Serial.println("Disco ready.");

  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS_PIN);
  touch.begin(touchSPI);
  touch.setRotation(1);

  memset(sparks, 0, sizeof(sparks));
  randomSeed(analogRead(0));
}

// ── Touch → button hit test ───────────────────────────────────────────────────
void handleTouch() {
  if (!touch.tirqTouched() || !touch.touched()) return;
  TS_Point p = touch.getPoint();
  int sx = map(p.y, TS_MINY, TS_MAXY, 0, 320);
  int sy = map(p.x, TS_MINX, TS_MAXX, 240, 0);

  // Dismiss scan results — any tap closes the overlay
  if (scanState == SCAN_RESULTS) {
    scanState = SCAN_IDLE;
    WiFi.scanDelete();
    delay(150);
    return;
  }

  // Connect button
  if (sx >= WIFI_BTN_X && sx <= WIFI_BTN_X + WIFI_BTN_W &&
      sy >= WIFI_BTN_Y && sy <= WIFI_BTN_Y + WIFI_BTN_H) {
    startWifiConnect();
    delay(120);
    return;
  }

  // Scan button
  if (sx >= SCAN_BTN_X && sx <= SCAN_BTN_X + SCAN_BTN_W &&
      sy >= SCAN_BTN_Y && sy <= SCAN_BTN_Y + SCAN_BTN_H) {
    if (scanState == SCAN_IDLE) {
      WiFi.scanNetworks(/*async=*/true);
      scanState = SCAN_RUNNING;
      Serial.println("WiFi scan started...");
    }
    delay(120);
    return;
  }
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  handleTouch();
  updateWifiStatus();

  // Check if async scan finished
  if (scanState == SCAN_RUNNING) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      scanCount = n;
      scanState = SCAN_RESULTS;
      Serial.printf("Scan done: %d networks\n", n);
    }
  }

  // Show scan results overlay instead of disco
  if (scanState == SCAN_RESULTS) {
    for (int half = 0; half < 2; half++)
      drawScanResults(half * 120);
    return;
  }

  // ── Normal disco render ───────────────────────────────────────────────────
  if (random(3) == 0) spawnSparkle();

  uint16_t wireHi = tft.color565(100, 100, 110);
  uint16_t wireLo = tft.color565(45,  45,  55);

  for (int half = 0; half < 2; half++) {
    int yOff = half * 120;

    drawBackground(yOff);

    int wireTop = 0 - yOff;
    int wireBot = BALL_CY - BALL_R - yOff;
    canvas.drawLine(BALL_CX,     wireTop, BALL_CX,     wireBot, wireHi);
    canvas.drawLine(BALL_CX - 1, wireTop, BALL_CX - 1, wireBot, wireLo);

    drawBeams(yOff);
    drawSparkles(yOff);
    drawDiscoBall(yOff);
    drawWifiDot(yOff);
    drawScanButton(yOff);

    canvas.pushSprite(0, yOff);
  }

  rotation  = fmodf(rotation  + 0.04f, 2.0f * M_PI);
  globalHue = fmodf(globalHue + 1.6f,  360.0f);
}

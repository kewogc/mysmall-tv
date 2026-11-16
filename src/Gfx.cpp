#include "Gfx.h"
#include "Platform.h"
#include "SunTimes.h"
#include "features/theme/FontCyr866.h"
#include "features/theme/Text866.h"
#include <Arduino_GFX_Library.h>
#include <SPI.h>

// The SmallTV's ST7789 has its CS line tied to GND and only latches SPI in
// **mode 3**. Arduino_GFX's stock Arduino_ST7789 forces SPI_MODE2 on the ESP8266
// (wrong clock edge for this panel), so the controller never initializes and the
// screen stays black even with the backlight on. Subclass begin() to force mode 3
// — matching the known-good GeekMagic community firmwares. (On ESP32 the base
// class already selects mode 3, so the override is harmless there.)

// Runtime panel colour order, read by the setRotation override below. The board
// header's TFT_BGR is the factory default for that variant; the Display tab can
// override it, because units of the same model do turn up with red and blue
// swapped in the controller.
static bool g_bgr = (TFT_BGR != 0);

class Arduino_ST7789_SmallTV : public Arduino_ST7789 {
 public:
  using Arduino_ST7789::Arduino_ST7789;   // inherit constructors
  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    _override_datamode = SPI_MODE3;
    return Arduino_TFT::begin(speed);
  }

  // Arduino_ST7789 hardcodes the MADCTL RGB order, so re-issue MADCTL with the
  // BGR bit (0x08) tracking g_bgr on every rotation change. The MX/MY/MV values
  // below are the base class's own mapping (ST7789_MADCTL_RGB is 0x00), so with
  // g_bgr false this writes exactly what the library would have written. Only
  // rotations 0-3 are used by the SmallTV (setRotation(r & 3)).
  void setRotation(uint8_t r) override {
    Arduino_TFT::setRotation(r);           // updates _rotation + width/height
    uint8_t madctl;
    switch (_rotation) {
      case 1:  madctl = ST7789_MADCTL_MX | ST7789_MADCTL_MV; break;
      case 2:  madctl = ST7789_MADCTL_MX | ST7789_MADCTL_MY; break;
      case 3:  madctl = ST7789_MADCTL_MY | ST7789_MADCTL_MV; break;
      default: madctl = 0; break;          // case 0
    }
    if (g_bgr) madctl |= 0x08;              // BGR
    _bus->beginWrite();
    _bus->writeC8D8(ST7789_MADCTL, madctl);
    _bus->endWrite();
  }

  // Russian text. The library's built-in font has no Cyrillic, so the two
  // overrides below add it for everything printed through this device.
  //
  // write() takes UTF-8 Russian letters as they stand in string literals: a
  // lead byte 0xD0/0xD1 is held until its continuation byte arrives, and the
  // pair goes on as one CP866 byte. Text already in CP866 (Text866.h) passes
  // through, because no Russian letter is 0xD0 or 0xD1 there.
  size_t write(uint8_t c) override {
    if (utf8Lead_) {
      const uint8_t lead = utf8Lead_;
      utf8Lead_ = 0;
      if ((c & 0xC0) == 0x80) {
        const uint8_t b = smalltv::cp866FromCodepoint((uint32_t(lead & 0x1F) << 6) | (c & 0x3F));
        return Arduino_ST7789::write(b ? b : uint8_t('?'));
      }
      Arduino_ST7789::write(lead);   // not a pair after all: print it as it was
    }
    if (c == 0xD0 || c == 0xD1) {
      utf8Lead_ = c;
      return 1;
    }
    return Arduino_ST7789::write(c);
  }

  // drawChar() draws a CP866 Russian letter from our table, in the cell and
  // with the scaling of the built-in font; any other character is the library's.
  void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg) override {
    const int k = smalltv::cyr866Index(c);
    if (k < 0) {
      Arduino_ST7789::drawChar(x, y, c, color, bg);
      return;
    }
    const int16_t sx = textsize_x, sy = textsize_y;
    startWrite();
    for (int8_t i = 0; i < GFX_FONT_W; ++i) {
      uint8_t line = i < 5 ? smalltv::kFontCyr866[k * 5 + i] : 0;   // sixth column is the gap
      for (int8_t j = 0; j < GFX_FONT_H; ++j, line >>= 1) {
        if (line & 1) writeFillRect(x + i * sx, y + j * sy, sx, sy, color);
        else if (bg != color) writeFillRect(x + i * sx, y + j * sy, sx, sy, bg);
      }
    }
    endWrite();
  }

 private:
  uint8_t utf8Lead_ = 0;
};

// ---- language --------------------------------------------------------------
static bool g_langRu = false;

void gfxSetLang(const char* code) { g_langRu = code && !strcmp(code, "ru"); }
bool gfxLangRu() { return g_langRu; }

// ---- colour correction ----------------------------------------------------
// Per-channel gain in percent, 100 = untouched. Kept as plain bytes so the
// all-default case is a single comparison on the hot drawing path.
static uint8_t g_rGain = 100, g_gGain = 100, g_bGain = 100;

uint16_t gfxTint(uint16_t c) {
  if (g_rGain == 100 && g_gGain == 100 && g_bGain == 100) return c;
  uint32_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  r = r * g_rGain / 100; if (r > 31) r = 31;
  g = g * g_gGain / 100; if (g > 63) g = 63;
  b = b * g_bGain / 100; if (b > 31) b = 31;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static Arduino_DataBus* bus = nullptr;
static Arduino_GFX*     gfx = nullptr;

Arduino_GFX* gfxDev() { return gfx; }

// ---------------------------------------------------------------------------
void gfxBegin(const Settings& s) {
#ifdef TFT_PWR_PIN
  // Boards with a switched panel power rail: energize the display
  // before anything else or the panel never comes up.
  pinMode(TFT_PWR_PIN, OUTPUT);
  digitalWrite(TFT_PWR_PIN, TFT_PWR_ON);
#endif
  // Backlight FIRST: do it before the panel/SPI init so the screen lights up even
  // if panel init has trouble. A dark backlight then means the sketch didn't get
  // this far (early crash / bad flash) — a useful boot indicator.
  pinMode(TFT_BL, OUTPUT);
  platformAnalogWriteInit(TFT_BL);
  gfxSetLang(s.clock.lang.c_str());
  gfxSetBrightness(s.brightness, s.backlightInverted);

#if defined(SMALLTV_FAMILY_ESP32)
  // Hardware SPI via the Arduino SPI library (IDF spi_master driver) on explicit
  // GPIOs. The register-level Arduino_ESP32SPI hangs in begin() on the C2, and
  // Arduino_SWSPI's fast-IO path doesn't cover the C2 — Arduino_HWSPI uses the
  // stock driver (what the working ESPHome config used) and honors SPI mode 3
  // (see the subclass). Pins come from the board header; a TFT_CS of -1 means
  // the panel's CS is tied to GND and is never toggled.
  bus = new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED, &SPI);
#else
  bus = new Arduino_HWSPI(TFT_DC, TFT_CS);   // ESP8266 HW-SPI (fixed SCLK/MOSI)
#endif
  // IPS=true so the panel colors are not inverted. The ST7789(V) has 240x320
  // RAM against 240x240 glass, so the rotation 2/3 row offset (TFT_ROW_OFFSET2,
  // 80) matters: without it a 180°-rotated image slides into the dead band.
  // Use the SmallTV variant so the SPI bus comes up in mode 3 (see class above).
  gfx = new Arduino_ST7789_SmallTV(bus, TFT_RST, 0 /*rotation*/, true /*IPS*/,
                                   TFT_WIDTH, TFT_HEIGHT,
                                   TFT_COL_OFFSET1, TFT_ROW_OFFSET1,
                                   TFT_COL_OFFSET2, TFT_ROW_OFFSET2);
  gfx->begin();
  // Colour order/inversion/gain before the first pixel, so the panel comes up
  // already corrected instead of flashing an uncorrected boot screen. This also
  // writes MADCTL for the configured rotation.
  gfxApplyColors(s);
  // Nothing in this UI ever wants wrapped text: overflowing labels used to
  // wrap around to x=0 on the next line (stray characters at the left edge).
  gfx->setTextWrap(false);
  gfx->fillScreen(C_BLACK);
}

void gfxSetBrightnessSteps(uint8_t steps, bool inverted) {
  analogWrite(TFT_BL, inverted ? 255 - steps : steps);
}

void gfxSetBrightness(uint8_t pct, bool inverted) {
  gfxSetBrightnessSteps(brightnessSteps(pct), inverted);
}

void gfxSetRotation(uint8_t r) {
  if (gfx) gfx->setRotation(r & 3);
}

void gfxApplyColors(const Settings& s) {
  g_rGain = s.display.rGain;
  g_gGain = s.display.gGain;
  g_bGain = s.display.bGain;
  g_bgr = (s.display.colorOrder == COLOR_ORDER_BGR) ? true
        : (s.display.colorOrder == COLOR_ORDER_RGB) ? false
                                                    : (TFT_BGR != 0);
  if (!gfx) return;
  gfx->setRotation(s.rotation & 3);   // re-issues MADCTL with the new order
  gfx->invertDisplay(s.display.invert);
}

// ---- text helpers (built-in 6x8 font, integer scaled) ---------------------
// Cells, not bytes: a Russian letter in a UTF-8 literal is two bytes, one cell.
int gfxTextW(const char* s, uint8_t size) { return (int)smalltv::textCells866(s) * GFX_FONT_W * size; }

void gfxDrawCentered(const char* s, int y, uint8_t size, uint16_t color) {
  if (!gfx) return;
  int x = (TFT_WIDTH - gfxTextW(s, size)) / 2;
  if (x < 0) x = 0;
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(s);
}

// Largest size (<= maxSize) whose rendered width fits within maxW.
uint8_t gfxFitSize(const char* s, int maxW, uint8_t maxSize) {
  for (uint8_t sz = maxSize; sz > 1; sz--) {
    if (gfxTextW(s, sz) <= maxW) return sz;
  }
  return 1;
}

// ---------------------------------------------------------------------------
void gfxBoot(const char* line1, const char* line2) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered(line1, 95, 3, C_WHITE);
  if (line2 && line2[0]) gfxDrawCentered(line2, 130, 2, C_GRAY);
}

void gfxApInfo(const char* ssid, const char* pass, const char* ip, const char* why) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered(TR("SETUP MODE", "НАСТРОЙКА"), 18, 3, C_YELLOW);
  if (why && why[0]) gfxDrawCentered(why, 44, gfxFitSize(why, 232, 2), C_RED);
  gfxDrawCentered(TR("Join WiFi:", "Сеть Wi-Fi:"), 64, 2, C_GRAY);
  gfxDrawCentered(ssid, 88, gfxFitSize(ssid, 232, 3), C_WHITE);
  if (pass && pass[0]) {
    gfxDrawCentered(TR("Password:", "Пароль:"), 124, 2, C_GRAY);
    gfxDrawCentered(pass, 146, gfxFitSize(pass, 232, 2), C_WHITE);
  } else {
    gfxDrawCentered(TR("(open network)", "(без пароля)"), 124, 2, C_GRAY);
  }
  gfxDrawCentered(TR("Then open:", "Затем откройте:"), 182, 2, C_GRAY);
  String url = String("http://") + ip;
  gfxDrawCentered(url.c_str(), 206, gfxFitSize(url.c_str(), 232, 2), C_GREEN);
}

void gfxStaInfo(const char* ssid, const char* ip, const char* host) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered(TR("CONNECTED", "ПОДКЛЮЧЕНО"), 18, 3, C_GREEN);
  gfxDrawCentered(TR("Network:", "Сеть:"), 62, 2, C_GRAY);
  gfxDrawCentered(ssid && ssid[0] ? ssid : "-", 84, gfxFitSize(ssid, 232, 3), C_WHITE);
  gfxDrawCentered(TR("Open in browser:", "В браузере:"), 126, 2, C_GRAY);
  // IP shown big (always fits at size 2); mDNS name below as a friendlier option.
  gfxDrawCentered(ip && ip[0] ? ip : "-", 150, gfxFitSize(ip, 232, 3), C_GREEN);
  if (host && host[0]) {
    String url = String("http://") + host + ".local";
    gfxDrawCentered(url.c_str(), 188, gfxFitSize(url.c_str(), 232, 2), C_GRAY);
  }
}

void gfxMessage(const char* title, const char* msg, uint16_t titleColor) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered(title, 90, 3, titleColor);
  if (msg && msg[0]) gfxDrawCentered(msg, 130, 2, C_GRAY);
}

// Persistent crash screen shown in safe mode (after an exception reset). Holds the
// crash PC + fault address still so they can be read, and the IP for OTA recovery.
void gfxCrash(const char* epc, const char* addr, const char* ip) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered(TR("CRASH", "СБОЙ"), 12, 4, C_RED);
  gfxDrawCentered("epc", 60, 2, C_GRAY);
  gfxDrawCentered(epc && epc[0] ? epc : "-", 80, 3, C_WHITE);
  gfxDrawCentered("addr", 124, 2, C_GRAY);
  gfxDrawCentered(addr && addr[0] ? addr : "-", 146, 2, C_WHITE);
  gfxDrawCentered(TR("OTA flash to fix:", "Прошейте по Wi-Fi:"), 182, 2, C_GRAY);
  gfxDrawCentered(ip && ip[0] ? ip : "-", 204, 2, C_GREEN);
}

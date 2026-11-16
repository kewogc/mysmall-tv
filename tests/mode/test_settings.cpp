// The "mode" key of a save or an import. A save that does not name a mode the
// build knows must leave the stored mode alone. Before, an empty or unknown
// text reset the mode to the first one. Built with the stand-in core
// headers of the fuzz target (see tests/mode/run.sh).
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include "Settings.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

FSClass LittleFS;
EspClass ESP;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond, ...)                                                   \
  do {                                                                     \
    g_checks++;                                                            \
    if (!(cond)) {                                                         \
      g_failed++;                                                          \
      printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond);               \
      printf(__VA_ARGS__);                                                 \
      printf("\n");                                                        \
    }                                                                      \
  } while (0)

static void apply(Settings& s, const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) { printf("bad test json: %s\n", json); g_failed++; return; }
  settingsApplyJson(s, doc.as<JsonObjectConst>());
}

// Start from a device that stored `mode`, then apply a save.
static uint8_t after(uint8_t stored, const char* json) {
  Settings s;
  s.setDefaults();
  s.mode = stored;
  apply(s, json);
  return s.mode;
}

static void testNamesSetTheMode() {
  CHECK(after(MODE_WEATHER, "{\"mode\":\"theme\"}") == MODE_THEME, "theme");
  CHECK(after(MODE_WEATHER, "{\"mode\":\"radar\"}") == MODE_RADAR, "radar");
  CHECK(after(MODE_WEATHER, "{\"mode\":\"usage\"}") == MODE_USAGE, "usage");
  CHECK(after(MODE_WEATHER, "{\"mode\":\"ha\"}") == MODE_HA, "ha");
  CHECK(after(MODE_WEATHER, "{\"mode\":\"carousel\"}") == MODE_CAROUSEL, "carousel");
  CHECK(after(MODE_RADAR, "{\"mode\":\"weather\"}") == MODE_WEATHER, "weather");
  CHECK(after(MODE_RADAR, "{\"mode\":\"Theme\"}") == MODE_THEME, "case is ignored");
}

static void testEmptyMode() {
  // The page sends "" when its mode list lacks the stored mode (a build
  // without that feature). The stored mode stays.
  CHECK(after(MODE_RADAR, "{\"mode\":\"\"}") == MODE_RADAR, "radar kept");
  CHECK(after(MODE_THEME, "{\"mode\":\"\"}") == MODE_THEME, "theme kept");
  CHECK(after(MODE_HA, "{\"mode\":\"\"}") == MODE_HA, "ha kept");
  CHECK(after(MODE_CAROUSEL, "{\"mode\":\"\"}") == MODE_CAROUSEL, "carousel kept");
}

static void testUnknownMode() {
  CHECK(after(MODE_USAGE, "{\"mode\":\"nonsense\"}") == MODE_USAGE, "unknown text kept");
  CHECK(after(MODE_USAGE, "{\"mode\":\"theme2\"}") == MODE_USAGE, "near miss kept");
}

static void testNoModeOrOtherType() {
  CHECK(after(MODE_HA, "{\"brightness\":40}") == MODE_HA, "no mode key");
  CHECK(after(MODE_HA, "{\"mode\":7}") == MODE_HA, "number");
  CHECK(after(MODE_HA, "{\"mode\":null}") == MODE_HA, "null");
}

static void testImport() {
  // An import goes through the same parser, onto the stored settings: an
  // export from a lean build with an empty mode does not move the mode.
  Settings s;
  s.setDefaults();
  apply(s, "{\"mode\":\"radar\",\"hostname\":\"smalltv-test\"}");
  CHECK(s.mode == MODE_RADAR, "stored radar");
  apply(s, "{\"mode\":\"\",\"hostname\":\"smalltv-test-2\"}");
  CHECK(s.mode == MODE_RADAR, "import with empty mode keeps radar");
  CHECK(s.hostname == "smalltv-test-2", "the rest of the import applies");
}

// The stock ticker modes are gone. Their names are unknown text now: a stored
// file that carries one loads as the default mode, a save or an import onto a
// running device keeps its mode. The ticker's own keys are read by nothing
// and are not written back.
static void testRemovedTickerModes() {
  const char* const names[] = {"stocks", "stocks2", "stock1", "stock2"};
  for (const char* name : names) {
    char json[256];
    snprintf(json, sizeof(json),
             "{\"mode\":\"%s\",\"carouselTicker\":false,\"carouselTicker2\":false,"
             "\"ticker\":{\"pollSec\":30,\"symbols\":[{\"symbol\":\"AAPL\"}]},\"ticker2\":{}}", name);
    Settings s;
    s.setDefaults();                 // what loadSettings() does before the file is applied
    apply(s, json);
    CHECK(s.mode == DEFAULT_MODE, "%s loads as the default mode", name);
    CHECK(s.mode == MODE_WEATHER, "the default mode is the weather");
    CHECK(after(MODE_THEME, json) == MODE_THEME, "%s in a save keeps the stored mode", name);

    JsonDocument out;
    settingsToJson(s, out.to<JsonObject>(), true);
    CHECK(out["ticker"].isNull() && out["ticker2"].isNull(), "no ticker objects are written");
    CHECK(out["carouselTicker"].isNull() && out["carouselTicker2"].isNull(), "no ticker carousel flags are written");
    CHECK(strcmp(out["mode"] | "", "weather") == 0, "the mode is written as weather");
  }
}

// An old flat file (before the nested layout) still seeds what is left: the
// Wi-Fi network and the usage poll period. Its ticker keys are ignored.
static void testLegacyFlatFile() {
  Settings s;
  s.setDefaults();
  apply(s, "{\"staSsid\":\"home-wifi-01\",\"staPass\":\"test-pass-01\",\"mode\":\"stocks\","
           "\"range\":\"5d\",\"points\":60,\"pollSec\":300,\"brightness\":80,\"rotation\":2}");
  CHECK(s.wifiCount == 1 && s.wifi[0].ssid == "home-wifi-01", "the network is kept");
  CHECK(s.usage.pollSec == 300, "the flat pollSec still seeds the usage slice");
  CHECK(s.brightness == 80 && s.rotation == 2, "shared keys apply");
  CHECK(s.mode == MODE_WEATHER, "the default mode");
}

int main() {
  testNamesSetTheMode();
  testRemovedTickerModes();
  testLegacyFlatFile();
  testEmptyMode();
  testUnknownMode();
  testNoModeOrOtherType();
  testImport();
  printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}

#include "WeatherMode.h"
#include <Arduino_GFX_Library.h>
#include "../../Platform.h"
#include "../../Gfx.h"
#include "../../Net.h"
#include "../../Clock.h"
#include "WeatherClient.h"

WeatherMode g_weatherMode;

void WeatherMode::begin(const Settings& s) {
  weatherInit(s);
  renderedLastOk_ = 0xFFFFFFFF;
  renderedError_ = false;
  needRender_ = true;
}

void WeatherMode::invalidate(const Settings& s) {
  weatherInit(s);
  weatherForceRefresh();
  renderedLastOk_ = 0xFFFFFFFF;
  needRender_ = true;
}

void WeatherMode::wake(const Settings& s) {
  g_abortFetch = false;
  needRender_ = true;
  weatherDeferNextFetch(800);
  render(s);
  needRender_ = false;
}

void WeatherMode::render(const Settings& s) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);

  const WeatherData& d = weatherGet();
  if (!d.valid) {
    gfxDrawCentered("Weather", 80, 3, C_WHITE);
    gfxDrawCentered(d.error ? "fetch error" : "loading...", 120, 2, C_GRAY);
    return;
  }

  // Header: Location / Title
  gfxDrawCentered("Seoul Weather", 8, 2, C_YELLOW);

  // Time / Date if available
  time_t now = time(nullptr);
  if (now > 1600000000) {
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", tmNow.tm_hour, tmNow.tm_min);
    gfxDrawCentered(timeBuf, 30, 2, C_GRAY);
  }

  // Big Current Temp: e.g. "12.8 C"
  char tempStr[20];
  snprintf(tempStr, sizeof(tempStr), "%.1f C", d.temp);
  gfxDrawCentered(tempStr, 58, 5, C_WHITE);

  // Weather Condition: e.g. "Partly Cloudy"
  const char* desc = weatherCodeToDesc(d.weatherCode);
  gfxDrawCentered(desc, 108, gfxFitSize(desc, 230, 3), C_GREEN);

  // Divider
  gfx->drawFastHLine(10, 140, TFT_WIDTH - 20, C_DGRAY);

  // Details
  // Min / Max
  char minMaxStr[32];
  snprintf(minMaxStr, sizeof(minMaxStr), "Min %.1f C  Max %.1f C", d.tempMin, d.tempMax);
  gfxDrawCentered(minMaxStr, 150, 2, C_WHITE);

  // Humidity
  char humStr[24];
  snprintf(humStr, sizeof(humStr), "Humidity: %d%%", d.humidity);
  gfxDrawCentered(humStr, 178, 2, C_BLUE);

  // Wind Speed
  char windStr[24];
  snprintf(windStr, sizeof(windStr), "Wind: %.1f km/h", d.windSpeed);
  gfxDrawCentered(windStr, 204, 2, C_GRAY);

  if (d.error) gfx->fillCircle(6, 6, 3, C_RED);
}

void WeatherMode::service(const Settings& s) {
  if (needRender_) {
    render(s);
    needRender_ = false;
  }

  weatherService(s);

  const WeatherData& d = weatherGet();
  if (d.lastOkMs != renderedLastOk_ || d.error != renderedError_) {
    needRender_ = true;
    renderedLastOk_ = d.lastOkMs;
    renderedError_ = d.error;
  }

  if (needRender_) {
    render(s);
    needRender_ = false;
  }
}

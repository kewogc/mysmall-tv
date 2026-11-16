#pragma once
#include <Arduino.h>
#include "WeatherData.h"
#include "../../Settings.h"

void weatherInit(const Settings& s);
void weatherService(const Settings& s);
void weatherForceRefresh();
void weatherDeferNextFetch(uint32_t delayMs);
const WeatherData& weatherGet();
const char* weatherCodeToDesc(uint8_t code);

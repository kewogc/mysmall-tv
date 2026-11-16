#pragma once
#include <Arduino.h>

struct WeatherData {
  float    temp;        // current temp (C)
  uint8_t  humidity;    // %
  uint8_t  weatherCode; // WMO code (0..99)
  float    windSpeed;   // km/h
  float    tempMax;     // today max (C)
  float    tempMin;     // today min (C)

  bool     valid;
  bool     error;
  uint32_t lastOkMs;
  uint32_t nextTryMs;
  uint8_t  fails;

  void clear() {
    temp = tempMax = tempMin = windSpeed = 0;
    humidity = weatherCode = 0;
    valid = error = false;
    lastOkMs = nextTryMs = 0;
    fails = 0;
  }
};

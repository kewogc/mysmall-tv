// Target.h — chip selection, checked once.
//
// The only board built from this repo is the GeekMagic SmallTV Pro (classic
// ESP32): platformio.ini defines SMALLTV_ESP32 (the chip) and
// SMALLTV_ESP32_PRO (the board).
//
// The firmware still carries code paths for two other chips, selected by
//   -D SMALLTV_ESP8266   ESP-12F
//   -D SMALLTV_ESP32C2   ESP32-C2 / ESP8684
// No env builds them, but the native tests in tests/ compile shared code with
// SMALLTV_ESP8266, so the check below still accepts any one of the three.
//
// The rest of the firmware tests the family macros defined here instead of
// listing chip macros:
//   SMALLTV_FAMILY_ESP32     C2 and classic ESP32 (Arduino core 3.x, mbedTLS)
//   SMALLTV_FAMILY_ESP8266   ESP8266 (Arduino core 2.x, BearSSL)
// Exactly one of the two is defined.
#pragma once

#if (defined(SMALLTV_ESP8266) + defined(SMALLTV_ESP32C2) + defined(SMALLTV_ESP32)) != 1
#error "Define exactly one of SMALLTV_ESP8266, SMALLTV_ESP32C2, SMALLTV_ESP32 (see platformio.ini)"
#endif

#if defined(SMALLTV_ESP32C2) || defined(SMALLTV_ESP32)
#define SMALLTV_FAMILY_ESP32 1
#else
#define SMALLTV_FAMILY_ESP8266 1
#endif

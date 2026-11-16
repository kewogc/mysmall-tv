#include "WeatherClient.h"
#include "../../Platform.h"
#include <ArduinoJson.h>

static WeatherData g_weather;
static bool g_fetching = false;

const char* weatherCodeToDesc(uint8_t code) {
  switch (code) {
    case 0:  return "Clear";
    case 1:  return "Mainly Clear";
    case 2:  return "Partly Cloudy";
    case 3:  return "Overcast";
    case 45:
    case 48: return "Foggy";
    case 51:
    case 53:
    case 55: return "Drizzle";
    case 61:
    case 63:
    case 65: return "Rain";
    case 71:
    case 73:
    case 75: return "Snow";
    case 77: return "Snow Grains";
    case 80:
    case 81:
    case 82: return "Rain Showers";
    case 85:
    case 86: return "Snow Showers";
    case 95:
    case 96:
    case 99: return "Thunderstorm";
    default: return "Cloudy";
  }
}

void weatherInit(const Settings& s) {
  g_weather.clear();
  g_weather.nextTryMs = millis();
  g_fetching = false;
}

void weatherForceRefresh() {
  g_weather.nextTryMs = millis();
  g_weather.fails = 0;
  g_fetching = false;
}

void weatherDeferNextFetch(uint32_t delayMs) {
  uint32_t target = millis() + delayMs;
  if ((int32_t)(target - g_weather.nextTryMs) > 0) {
    g_weather.nextTryMs = target;
  }
  g_fetching = false;
}

const WeatherData& weatherGet() {
  return g_weather;
}

static bool fetchWttr(float lat, float lon, uint16_t timeoutMs, WeatherData& d) {
  if (g_abortFetch) return false;

  // wttr.in format=j2 provides compact 3KB JSON containing current + daily min/max
  char url[128];
  snprintf(url, sizeof(url), "http://wttr.in/%.4f,%.4f?format=j2", lat, lon);

  std::unique_ptr<WiFiClient> client(new WiFiClient());
  if (!client) return false;

  HTTPClient http;
  uint16_t t = (timeoutMs > 3000) ? 3000 : timeoutMs;
  http.setTimeout(t);
  http.setReuse(false);
  http.useHTTP10(true);
  if (!http.begin(*client, url)) return false;
  http.setUserAgent(F("curl/7.88.1"));
  http.addHeader("Accept", "application/json");

  int code = http.GET();
  if (code != HTTP_CODE_OK || g_abortFetch) {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument filter;
  filter["current_condition"][0]["temp_C"] = true;
  filter["current_condition"][0]["humidity"] = true;
  filter["current_condition"][0]["windspeedKmph"] = true;
  filter["current_condition"][0]["weatherDesc"][0]["value"] = true;
  filter["weather"][0]["maxtempC"] = true;
  filter["weather"][0]["mintempC"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload.c_str(), DeserializationOption::Filter(filter));
  if (err) return false;

  JsonObjectConst cur = doc["current_condition"][0];
  if (cur.isNull()) return false;

  const char* tempStr = cur["temp_C"] | "0";
  d.temp = atof(tempStr);

  const char* humStr = cur["humidity"] | "0";
  d.humidity = atoi(humStr);

  const char* windStr = cur["windspeedKmph"] | "0";
  d.windSpeed = atof(windStr);

  const char* descStr = cur["weatherDesc"][0]["value"] | "Clear";
  d.weatherCode = 0;
  if (strstr(descStr, "Cloud") || strstr(descStr, "Overcast")) d.weatherCode = 2;
  else if (strstr(descStr, "Rain") || strstr(descStr, "Drizzle")) d.weatherCode = 61;
  else if (strstr(descStr, "Snow")) d.weatherCode = 71;
  else if (strstr(descStr, "Mist") || strstr(descStr, "Fog") || strstr(descStr, "Haze")) d.weatherCode = 45;
  else if (strstr(descStr, "Thunder")) d.weatherCode = 95;

  const char* maxStr = doc["weather"][0]["maxtempC"] | tempStr;
  const char* minStr = doc["weather"][0]["mintempC"] | tempStr;
  d.tempMax = atof(maxStr);
  d.tempMin = atof(minStr);

  d.valid = true;
  d.error = false;
  d.lastOkMs = millis();
  return true;
}

void weatherService(const Settings& s) {
  if (g_fetching) return;
  if ((int32_t)(millis() - g_weather.nextTryMs) < 0) return;

  float lat = s.clock.lat;
  float lon = s.clock.lon;
  if (lat == 0.0f && lon == 0.0f) {
    lat = 37.566f;
    lon = 126.9784f; // Default Seoul
  }

  uint32_t pollInterval = (uint32_t)(s.weatherPollMin ? s.weatherPollMin : 10) * 60000UL;

  g_abortFetch = false;
  g_fetching = true;
  if (fetchWttr(lat, lon, s.httpTimeout, g_weather)) {
    g_weather.valid = true;
    g_weather.error = false;
    g_weather.lastOkMs = millis();
    g_weather.fails = 0;
    g_weather.nextTryMs = millis() + pollInterval;
  } else {
    g_weather.error = true;
    if (g_weather.fails < 250) g_weather.fails++;
    uint32_t back = 15000UL; // 15 sec backoff
    for (uint8_t i = 1; i < g_weather.fails; i++) back <<= 1;
    if (back > pollInterval) back = pollInterval;
    g_weather.nextTryMs = millis() + back;
  }
  g_fetching = false;
}

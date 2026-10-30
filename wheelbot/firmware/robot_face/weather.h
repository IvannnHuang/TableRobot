#ifndef _ROBOT_WEATHER_H
#define _ROBOT_WEATHER_H

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"

// Current weather from Open-Meteo (free, no API key needed).
// With AUTO_LOCATION the location is looked up from the internet connection (ip-api.com, free,
// no key) and the time zone comes from the weather reply for that location.
// Everything runs in a background task, so the eyes and motors never freeze while waiting.

struct Weather {
  bool valid = false;
  float temperature = 0;
  int code = 0;      // WMO weather code
  bool isDay = true;
};

Weather weatherNow;
char locationLat[16] = WEATHER_LAT;
char locationLon[16] = WEATHER_LON;
char locationCity[32] = "";        // set when the location was found automatically
portMUX_TYPE weatherMux = portMUX_INITIALIZER_UNLOCKED;

// UTC offset handed from the weather task to the main loop, see applyAutoTimeZone()
volatile long pendingUtcOffset = 0;
volatile bool utcOffsetPending = false;

// Thread-safe copy of the latest weather
Weather getWeather() {
  taskENTER_CRITICAL(&weatherMux);
  Weather w = weatherNow;
  taskEXIT_CRITICAL(&weatherMux);
  return w;
}

// Thread-safe copy of the auto-detected city, "" if not found (yet)
String getLocationCity() {
  char city[sizeof(locationCity)];
  taskENTER_CRITICAL(&weatherMux);
  memcpy(city, locationCity, sizeof(city));
  taskEXIT_CRITICAL(&weatherMux);
  return String(city);
}

// Short description for a WMO weather code
const char *weatherText(int code) {
  if (code == 0) return "Clear";
  if (code <= 2) return "Fair";    // 1 mostly clear, 2 partly cloudy
  if (code == 3) return "Cloudy";
  if (code <= 48) return "Fog";
  if (code <= 57) return "Drizzle";
  if (code <= 67) return "Rain";
  if (code <= 77) return "Snow";
  if (code <= 82) return "Showers";
  if (code <= 86) return "Snow";
  return "Storm";
}

// Finds "key": after position from and returns the number behind it
float jsonNumber(const String &body, const char *key, int from, bool &ok) {
  String k = String("\"") + key + "\":";
  int i = body.indexOf(k, from);
  if (i < 0) {
    ok = false;
    return 0;
  }
  return body.substring(i + k.length()).toFloat();
}

// Finds "key":"..." and returns the text between the quotes
String jsonText(const String &body, const char *key) {
  String k = String("\"") + key + "\":\"";
  int i = body.indexOf(k);
  if (i < 0) return "";
  i += k.length();
  int end = body.indexOf('"', i);
  return end < 0 ? "" : body.substring(i, end);
}

// Look up latitude, longitude and city from the public IP address of the WiFi network
bool lookupLocation() {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, "http://ip-api.com/json/?fields=status,city,lat,lon")) return false;

  bool ok = false;
  int status = http.GET();
  if (status == 200) {
    String body = http.getString();
    ok = body.indexOf("\"success\"") >= 0;
    float lat = jsonNumber(body, "lat", 0, ok);
    float lon = jsonNumber(body, "lon", 0, ok);
    String city = jsonText(body, "city");
    if (ok) {
      char latText[16], lonText[16], cityText[sizeof(locationCity)];
      snprintf(latText, sizeof(latText), "%.4f", lat);
      snprintf(lonText, sizeof(lonText), "%.4f", lon);
      snprintf(cityText, sizeof(cityText), "%s", city.c_str());
      taskENTER_CRITICAL(&weatherMux);
      memcpy(locationLat, latText, sizeof(latText));
      memcpy(locationLon, lonText, sizeof(lonText));
      memcpy(locationCity, cityText, sizeof(cityText));
      taskEXIT_CRITICAL(&weatherMux);
      Serial.printf("Location: %s (%s, %s)\n", cityText, latText, lonText);
    }
  } else {
    Serial.printf("Location lookup failed: %d\n", status);
  }
  http.end();
  return ok;
}

bool fetchWeather() {
  WiFiClientSecure client;
  client.setInsecure(); // weather data isn't secret, skip certificate checking
  HTTPClient http;
  http.setTimeout(10000);

  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + locationLat
             + "&longitude=" + locationLon
             + "&current=temperature_2m,weather_code,is_day&timezone=auto"
             + (WEATHER_FAHRENHEIT ? "&temperature_unit=fahrenheit" : "");
  if (!http.begin(client, url)) return false;

  int status = http.GET();
  bool ok = false;
  if (status == 200) {
    String body = http.getString();

    // UTC offset of the location right now (already includes daylight saving time)
    bool offsetOk = true;
    long offset = (long)jsonNumber(body, "utc_offset_seconds", 0, offsetOk);
    if (offsetOk) {
      pendingUtcOffset = offset;
      utcOffsetPending = true;
    }

    int current = body.indexOf("\"current\":{"); // skip "current_units", which has the same keys
    if (current >= 0) {
      ok = true;
      Weather w;
      w.temperature = jsonNumber(body, "temperature_2m", current, ok);
      w.code = (int)jsonNumber(body, "weather_code", current, ok);
      w.isDay = jsonNumber(body, "is_day", current, ok) != 0;
      w.valid = ok;
      if (ok) {
        taskENTER_CRITICAL(&weatherMux);
        weatherNow = w;
        taskEXIT_CRITICAL(&weatherMux);
        Serial.printf("Weather: %.1f, %s\n", w.temperature, weatherText(w.code));
      }
    }
  } else {
    Serial.printf("Weather request failed: %d\n", status);
  }
  http.end();
  return ok;
}

// Set by the main loop while a phone is using the web page. The C3 has a single core, and the
// HTTPS weather request keeps it busy for a few seconds, which makes driving laggy, so it waits.
volatile bool weatherHold = false;

void weatherTask(void *) {
  bool locationFound = false;
  for (;;) {
    while (weatherHold) vTaskDelay(pdMS_TO_TICKS(1000));
    if (WiFi.status() == WL_CONNECTED) {
      if (AUTO_LOCATION && !locationFound) locationFound = lookupLocation(); // retried until it works
      if (fetchWeather()) {
        vTaskDelay(pdMS_TO_TICKS(WEATHER_REFRESH_MIN * 60000UL));
        continue;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(30000)); // retry in 30 s
  }
}

void startWeather() {
  xTaskCreate(weatherTask, "weather", 12288, nullptr, 1, nullptr);
}

// Call from the main loop: with AUTO_LOCATION, switch the clock to the time zone of the location.
// The offset is refreshed with every weather update, so daylight saving changes follow within
// WEATHER_REFRESH_MIN minutes.
void applyAutoTimeZone() {
  if (!AUTO_LOCATION || !utcOffsetPending) return;
  utcOffsetPending = false;

  static long applied = LONG_MIN;
  long offset = pendingUtcOffset;
  if (offset == applied) return;
  applied = offset;

  // POSIX counts hours west of UTC, so the sign is flipped: UTC-4 is "UTC+4:00"
  long west = -offset;
  char tz[24];
  snprintf(tz, sizeof(tz), "UTC%c%ld:%02ld", west < 0 ? '-' : '+', labs(west) / 3600, (labs(west) % 3600) / 60);
  setenv("TZ", tz, 1);
  tzset();
  Serial.printf("Time zone: UTC%+.1f h\n", offset / 3600.0);
}

#endif

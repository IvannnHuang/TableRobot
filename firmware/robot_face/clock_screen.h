#ifndef _ROBOT_CLOCK_SCREEN_H
#define _ROBOT_CLOCK_SCREEN_H

#include <Adafruit_SSD1306.h>
#include <time.h>
#include "config.h"
#include "weather.h"

// Sleep screen: date, big clock and weather.
//
// OLED burn-in protection (OLED pixels wear out the longer and brighter they are lit):
//  - the screen is dimmed to the lowest brightness while asleep
//  - the whole layout moves a few pixels to a random spot every PIXEL_SHIFT_SEC
//  - the clock colon blinks, so the middle of the screen isn't lit constantly
//  - the screen is switched fully off during night hours (NIGHT_OFF_START..NIGHT_OFF_END)
//
// Layout fits the two-color ELEGOO modules too: the date stays inside the top 16 rows (yellow),
// the clock and weather in the bottom 48 rows (blue).
class ClockScreen
{
public:

  ClockScreen(Adafruit_SSD1306 &display) : display(display) {}

  void enter() {
    display.ssd1306_command(SSD1306_DISPLAYON);
    display.dim(true);
    panelOff = false;
    lastDraw = 0;
    newShift();
  }

  void leave() {
    display.ssd1306_command(SSD1306_DISPLAYON);
    display.dim(false);
    panelOff = false;
  }

  // Call every loop while asleep
  void update() {
    unsigned long now = millis();
    if (lastDraw != 0 && now - lastDraw < 500) return; // redraw twice a second for the blinking colon
    lastDraw = now;
    if (now - lastShift >= PIXEL_SHIFT_SEC * 1000UL) newShift();

    struct tm t;
    bool haveTime = getLocalTime(&t, 0);

    // Screen fully off at night
    bool night = haveTime && isNight(t.tm_hour);
    if (night != panelOff) {
      display.ssd1306_command(night ? SSD1306_DISPLAYOFF : SSD1306_DISPLAYON);
      panelOff = night;
    }
    if (night) return;

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextWrap(false);
    drawDate(t, haveTime);
    drawTime(t, haveTime);
    drawWeather();
    display.display();
  }

private:

  Adafruit_SSD1306 &display;
  unsigned long lastDraw = 0;
  unsigned long lastShift = 0;
  int dx = 0, dy = 0; // current pixel shift
  bool panelOff = false;

  void newShift() {
    dx = random(-3, 4);
    dy = random(-2, 3);
    lastShift = millis();
  }

  static bool isNight(int hour) {
    if (NIGHT_OFF_START == NIGHT_OFF_END) return false;
    if (NIGHT_OFF_START > NIGHT_OFF_END) return hour >= NIGHT_OFF_START || hour < NIGHT_OFF_END;
    return hour >= NIGHT_OFF_START && hour < NIGHT_OFF_END;
  }

  // Width in pixels of text in the built-in font
  static int textWidth(const char *text, int size) {
    int len = strlen(text);
    return len == 0 ? 0 : len * 6 * size - size;
  }

  void drawCentered(const char *text, int size, int y) {
    display.setTextSize(size);
    display.setCursor((SCREEN_WIDTH - textWidth(text, size)) / 2 + dx, y);
    display.print(text);
  }

  void drawDate(struct tm &t, bool haveTime) {
    char buf[24];
    if (haveTime) strftime(buf, sizeof(buf), "%a, %b %d", &t);
    else if (strlen(HOME_WIFI_SSID) == 0) strcpy(buf, "No WiFi set");
    else if (WiFi.status() != WL_CONNECTED) strcpy(buf, "Connecting...");
    else strcpy(buf, "Syncing time...");
    drawCentered(buf, 1, 3 + dy);
  }

  void drawTime(struct tm &t, bool haveTime) {
    char buf[8];
    const char *ampm = nullptr;
    if (!haveTime) {
      strcpy(buf, "--:--");
    } else {
      char colon = (t.tm_sec % 2 == 0) ? ':' : ' ';
      if (CLOCK_24H) {
        snprintf(buf, sizeof(buf), "%02d%c%02d", t.tm_hour, colon, t.tm_min);
      } else {
        int hour = t.tm_hour % 12;
        if (hour == 0) hour = 12;
        snprintf(buf, sizeof(buf), "%d%c%02d", hour, colon, t.tm_min);
        ampm = t.tm_hour < 12 ? "AM" : "PM";
      }
    }
    int w = textWidth(buf, 3);
    int x = (SCREEN_WIDTH - w) / 2 + dx;
    display.setTextSize(3);
    display.setCursor(x, 18 + dy);
    display.print(buf);
    if (ampm) {
      display.setTextSize(1);
      display.setCursor(x + w + 2, 32 + dy);
      display.print(ampm);
    }
  }

  void drawWeather() {
    Weather w = getWeather();
    if (!w.valid) {
      drawCentered(strlen(HOME_WIFI_SSID) == 0 ? "Set WiFi in config.h" : "No weather yet", 1, 51 + dy);
      return;
    }

    // [icon] 54°F Cloudy
    char temp[8];
    snprintf(temp, sizeof(temp), "%d", (int)lroundf(w.temperature));
    const char *desc = weatherText(w.code);
    int tw = textWidth(temp, 2);
    int total = 16 + 4 + tw + 11 + 5 + textWidth(desc, 1);
    int x = (SCREEN_WIDTH - total) / 2 + dx;
    int y = 46 + dy;

    drawWeatherIcon(w.code, w.isDay, x, y);
    x += 20;
    display.setTextSize(2);
    display.setCursor(x, y + 1);
    display.print(temp);
    x += tw;
    display.drawCircle(x + 3, y + 2, 1, SSD1306_WHITE); // degree sign
    display.setTextSize(1);
    display.setCursor(x + 6, y + 1);
    display.print(WEATHER_FAHRENHEIT ? "F" : "C");
    display.setCursor(x + 16, y + 5);
    display.print(desc);
  }


  //// WEATHER ICONS, 16x16 pixels with top left corner at x, y ////

  void sun(int cx, int cy, int r) {
    display.fillCircle(cx, cy, r, SSD1306_WHITE);
    for (int i = 0; i < 8; i++) {
      float a = i * PI / 4;
      display.drawLine(cx + cos(a) * (r + 2), cy + sin(a) * (r + 2),
                       cx + cos(a) * (r + 3), cy + sin(a) * (r + 3), SSD1306_WHITE);
    }
  }

  void moon(int cx, int cy, int r) {
    display.fillCircle(cx, cy, r, SSD1306_WHITE);
    display.fillCircle(cx + r / 2 + 1, cy - r / 2, r - 1, SSD1306_BLACK);
  }

  // Cloud about 15x10 pixels; grow > 0 draws a bigger version (used as a black outline)
  void cloud(int x, int y, uint16_t color, int grow = 0) {
    display.fillCircle(x + 5, y + 5, 3 + grow, color);
    display.fillCircle(x + 10, y + 4, 4 + grow, color);
    display.fillRoundRect(x + 1 - grow, y + 5 - grow, 15 + 2 * grow, 5 + 2 * grow, 2, color);
  }

  void drawWeatherIcon(int code, bool isDay, int x, int y) {
    if (code == 0) { // clear
      if (isDay) sun(x + 8, y + 8, 4);
      else moon(x + 8, y + 8, 6);
    } else if (code <= 2) { // partly cloudy
      if (isDay) sun(x + 5, y + 5, 3);
      else moon(x + 5, y + 5, 4);
      cloud(x, y + 5, SSD1306_BLACK, 1);
      cloud(x, y + 5, SSD1306_WHITE);
    } else if (code == 3) { // overcast
      cloud(x, y + 3, SSD1306_WHITE);
    } else if (code <= 48) { // fog
      display.drawFastHLine(x + 2, y + 4, 12, SSD1306_WHITE);
      display.drawFastHLine(x, y + 7, 14, SSD1306_WHITE);
      display.drawFastHLine(x + 3, y + 10, 13, SSD1306_WHITE);
      display.drawFastHLine(x + 1, y + 13, 11, SSD1306_WHITE);
    } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) { // snow
      cloud(x, y, SSD1306_WHITE);
      for (int i = 0; i < 3; i++) {
        int sx = x + 3 + i * 5, sy = y + 13 - (i % 2) * 2;
        display.drawFastHLine(sx - 1, sy, 3, SSD1306_WHITE);
        display.drawFastVLine(sx, sy - 1, 3, SSD1306_WHITE);
      }
    } else if (code >= 95) { // storm
      cloud(x, y, SSD1306_WHITE);
      display.drawLine(x + 9, y + 10, x + 6, y + 13, SSD1306_WHITE);
      display.drawLine(x + 6, y + 13, x + 10, y + 13, SSD1306_WHITE);
      display.drawLine(x + 10, y + 13, x + 7, y + 15, SSD1306_WHITE);
    } else { // drizzle, rain, showers
      cloud(x, y, SSD1306_WHITE);
      for (int i = 0; i < 4; i++) {
        display.drawLine(x + 3 + i * 3, y + 11, x + 2 + i * 3, y + 14, SSD1306_WHITE);
      }
    }
  }
};

#endif

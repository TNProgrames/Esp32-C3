#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

class DisplayManager {
  private:
    std::vector<Adafruit_SSD1306*> oleds;

  public:
    void init(JsonArray displays) {
      // تنظيف الشاشات القديمة
      for (auto oled : oleds) delete oled;
      oleds.clear();

      for (JsonObject d : displays) {
        String driver = d["driver"];
        if (driver == "SSD1306") {
          int sda = d["pins"]["sda"] | 21;
          int scl = d["pins"]["scl"] | 22;
          
          Wire.begin(sda, scl);
          Adafruit_SSD1306* oled = new Adafruit_SSD1306(128, 64, &Wire, -1);
          
          if(oled->begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
            oled->clearDisplay();
            oled->setTextSize(1);
            oled->setTextColor(WHITE);
            oled->setCursor(0,0);
            oled->println("System Loaded...");
            oled->display();
            oleds.push_back(oled);
            Serial.println("Display Added: SSD1306");
          }
        }
      }
    }

    // دالة لكتابة نص على كل الشاشات
    void printMsg(String msg) {
      for (auto oled : oleds) {
        oled->clearDisplay();
        oled->setCursor(0,0);
        oled->println(msg);
        oled->display();
      }
    }
};

#endif
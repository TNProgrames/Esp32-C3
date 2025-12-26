#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <vector>

struct SensorInstance {
  int id;
  String name;
  String type;
  int pin;
  unsigned long lastReadTime;
  unsigned long interval;
  float lastValue;
  void* obj;
};

class SensorManager {
  private:
    std::vector<SensorInstance> sensors;

  public:
    void init(JsonArray sensorConfig) {
      // تنظيف الذاكرة
      for (auto &s : sensors) {
          if (s.type == "DHT" && s.obj != nullptr) delete (DHT*)s.obj;
      }
      sensors.clear();

      int idCounter = 0;
      for (JsonObject s : sensorConfig) {
        SensorInstance newSensor;
        newSensor.id = idCounter++;
        newSensor.name = s["name"] | ("Sensor " + String(newSensor.id));
        newSensor.type = s["driver"].as<String>();
        newSensor.pin = s["pin"];
        newSensor.interval = s["interval_ms"] | 2000;
        newSensor.lastReadTime = 0;
        newSensor.lastValue = 0.0;
        newSensor.obj = nullptr;

        if (newSensor.type == "DHT") {
          DHT* dht = new DHT(newSensor.pin, DHT11); // افتراضياً DHT11
          dht->begin();
          newSensor.obj = (void*)dht;
        } else {
           pinMode(newSensor.pin, INPUT);
        }
        
        sensors.push_back(newSensor);
        Serial.printf("Sensor Added: %s on Pin %d\n", newSensor.type.c_str(), newSensor.pin);
      }
    }

    void loop() {
      for (auto &s : sensors) {
        if (millis() - s.lastReadTime >= s.interval) {
          s.lastReadTime = millis();
          readSensor(s);
        }
      }
    }

    void readSensor(SensorInstance &s) {
      if (s.type == "DHT") {
        DHT* dht = (DHT*)s.obj;
        float t = dht->readTemperature();
        if (!isnan(t)) s.lastValue = t;
      } 
      else if (s.type == "LDR" || s.type == "POT") {
        s.lastValue = analogRead(s.pin);
      }
      else if (s.type == "DIGITAL") {
        s.lastValue = digitalRead(s.pin);
      }
    }

    String getJson() {
      String json = "[";
      for (size_t i = 0; i < sensors.size(); i++) {
        if (i > 0) json += ",";
        json += "{\"id\":" + String(sensors[i].id) + ",";
        json += "\"val\":" + String(sensors[i].lastValue) + "}";
      }
      json += "]";
      return json;
    }
};

#endif
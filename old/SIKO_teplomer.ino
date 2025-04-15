
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include "secrets.h" // Obsahuje WIFI_SSID, WIFI_PASSWORD
#include <ArduinoJson.h>

WebServer server(80);

float temperature = 22.5; // Simulovaná teplota
float comfortMin = 21.0;
float comfortMax = 24.0;

unsigned long lastUpdateTime = 0;
float history[288]; // Teploty za 24 h po 5 min
int historyIndex = 0;

// EEPROM adresy pro ukládání rozsahu komfortní teploty
#define EEPROM_COMFORT_MIN 0
#define EEPROM_COMFORT_MAX 4

void saveComfortToEEPROM() {
  EEPROM.put(EEPROM_COMFORT_MIN, comfortMin);
  EEPROM.put(EEPROM_COMFORT_MAX, comfortMax);
  EEPROM.commit();
}

void loadComfortFromEEPROM() {
  EEPROM.get(EEPROM_COMFORT_MIN, comfortMin);
  EEPROM.get(EEPROM_COMFORT_MAX, comfortMax);
}

String selectMemeURL() {
  if (temperature < comfortMin - 2) return "https://raw.githubusercontent.com/marek9336/SIKO_teplomer/main/Pictures/zima_3.png";
  if (temperature < comfortMin) return "https://raw.githubusercontent.com/marek9336/SIKO_teplomer/main/Pictures/zima_2.png";
  if (temperature < comfortMin + 0.5) return "https://raw.githubusercontent.com/marek9336/SIKO_teplomer/main/Pictures/zima_1.png";
  if (temperature > comfortMax + 2) return "https://raw.githubusercontent.com/marek9336/SIKO_teplomer/main/Pictures/horko_3.png";
  if (temperature > comfortMax) return "https://raw.githubusercontent.com/marek9336/SIKO_teplomer/main/Pictures/horko_2.png";
  if (temperature > comfortMax - 0.5) return "https://raw.githubusercontent.com/marek9336/SIKO_teplomer/main/Pictures/horko_1.png";
  return "https://raw.githubusercontent.com/marek9336/SIKO_teplomer/main/Pictures/ok_1.png";
}

void handleTemp() {
  StaticJsonDocument<100> doc;
  doc["temperature"] = temperature;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleHistory() {
  StaticJsonDocument<1500> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < 288; i++) {
    arr.add(history[i]);
  }
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleStatus() {
  StaticJsonDocument<200> doc;
  doc["uptime"] = millis() / 1000;
  doc["version"] = "v1.0";
  doc["comfortMin"] = comfortMin;
  doc["comfortMax"] = comfortMax;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleMeme() {
  StaticJsonDocument<200> doc;
  doc["meme"] = selectMemeURL();
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleSetComfort() {
  if (server.method() == HTTP_POST) {
    StaticJsonDocument<200> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      server.send(400, "application/json", "{"error":"Bad JSON"}");
      return;
    }
    comfortMin = doc["comfortMin"] | comfortMin;
    comfortMax = doc["comfortMax"] | comfortMax;
    saveComfortToEEPROM();
    server.send(200, "application/json", "{"status":"updated"}");
  } else {
    server.send(405, "application/json", "{"error":"Method Not Allowed"}");
  }
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(8);
  loadComfortFromEEPROM();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");

  server.on("/api/temp", handleTemp);
  server.on("/api/history", handleHistory);
  server.on("/api/status", handleStatus);
  server.on("/api/meme", handleMeme);
  server.on("/api/setcomfort", handleSetComfort);

  server.begin();
}

void loop() {
  server.handleClient();
  if (millis() - lastUpdateTime > 300000) {
    history[historyIndex] = temperature;
    historyIndex = (historyIndex + 1) % 288;
    lastUpdateTime = millis();
  }
}

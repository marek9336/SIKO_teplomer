// Tento soubor obsahuje HTML GUI verzi

#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "secrets.h"
#include <ArduinoJson.h>

#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
WebServer server(80);

float temperature = 0.0;
float calibration = 0.0;
float comfortMin = 21.0;
float comfortMax = 24.0;
unsigned long lastUpdateTime = 0;
float history[288];
int historyIndex = 0;

#define EEPROM_COMFORT_MIN 0
#define EEPROM_COMFORT_MAX 4
#define EEPROM_CALIBRATION 8

void saveComfortToEEPROM() {
  EEPROM.put(EEPROM_COMFORT_MIN, comfortMin);
  EEPROM.put(EEPROM_COMFORT_MAX, comfortMax);
  EEPROM.put(EEPROM_CALIBRATION, calibration);
  EEPROM.commit();
}
void loadComfortFromEEPROM() {
  EEPROM.get(EEPROM_COMFORT_MIN, comfortMin);
  EEPROM.get(EEPROM_COMFORT_MAX, comfortMax);
  EEPROM.get(EEPROM_CALIBRATION, calibration);
}
void readTemperature() {
  sensors.requestTemperatures();
  float rawTemp = sensors.getTempCByIndex(0);
  if (rawTemp != DEVICE_DISCONNECTED_C) {
    temperature = rawTemp + calibration;
  }
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
  doc["calibration"] = calibration;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}
void handleHistory() {
  StaticJsonDocument<1500> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < 288; i++) arr.add(history[i]);
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}
void handleStatus() {
  StaticJsonDocument<200> doc;
  doc["uptime"] = millis() / 1000;
  doc["version"] = "v2.0";
  doc["comfortMin"] = comfortMin;
  doc["comfortMax"] = comfortMax;
  doc["calibration"] = calibration;
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
    if (err) { server.send(400, "application/json", "{\"error\":\"Bad JSON\"}"); return; }
    comfortMin = doc["comfortMin"] | comfortMin;
    comfortMax = doc["comfortMax"] | comfortMax;
    calibration = doc["calibration"] | calibration;
    saveComfortToEEPROM();
    server.send(200, "application/json", "{\"status\":\"updated\"}");
  } else {
    server.send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
  }
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(16);
  sensors.begin();
  loadComfortFromEEPROM();
  readTemperature();

  WiFi.setHostname("ESP32_Temp");
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi připojeno!");
  Serial.print("ESP IP adresa: ");
  Serial.println(WiFi.localIP());
  Serial.print("ESP MAC adresa: ");
  Serial.println(WiFi.macAddress());
  Serial.print("ESP hostname: ");
  Serial.println(WiFi.getHostname());
  Serial.print("ESP SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("ESP RSSI: ");
  Serial.println(WiFi.RSSI());
  Serial.print("ESP BSSID: ");
  Serial.println(WiFi.BSSIDstr());
  Serial.print("ESP channel: ");
  Serial.println(WiFi.channel());

  server.on("/api/temp", handleTemp);
  server.on("/api/history", handleHistory);
  server.on("/api/status", handleStatus);
  server.on("/api/meme", handleMeme);
  server.on("/api/setcomfort", handleSetComfort);

  server.on("/", []() {
    server.send_P(200, "text/html", index_html);
  });

  server.begin();
}

void loop() {
  server.handleClient();
  if (millis() - lastUpdateTime > 300000) {
    readTemperature();
    history[historyIndex] = temperature;
    historyIndex = (historyIndex + 1) % 288;
    lastUpdateTime = millis();
  }
}

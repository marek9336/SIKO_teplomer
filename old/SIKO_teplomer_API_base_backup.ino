
#include <WiFi.h>
#include <WebServer.h>
#include "secrets.h"  // obsahuje token a přístup k repo
#include <ArduinoJson.h>

WebServer server(80);

// Simulovaná teplota (v reálném kódu bude čtena z čidla)
float temperature = 23.5;
unsigned long lastUpdateTime = 0;
float history[288]; // 24h historie po 5 minutách
int historyIndex = 0;

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
  doc["temp"] = temperature;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleMeme() {
  String memeUrl;
  if (temperature < 21) memeUrl = "https://raw.githubusercontent.com/marek9336/SIKO_teplomer/main/Pictures/zima_1.png";
  else if (temperature > 26) memeUrl = "https://raw.githubusercontent.com/marek9336/SIKO_teplomer/main/Pictures/horko_3.png";
  else memeUrl = "https://raw.githubusercontent.com/marek9336/SIKO_teplomer/main/Pictures/ok_1.png";

  StaticJsonDocument<200> doc;
  doc["meme"] = memeUrl;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
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

  server.begin();
}

void loop() {
  server.handleClient();
  if (millis() - lastUpdateTime > 300000) { // každých 5 minut
    history[historyIndex] = temperature;
    historyIndex = (historyIndex + 1) % 288;
    lastUpdateTime = millis();
  }
}

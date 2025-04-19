#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "secrets.h"
#include <ArduinoJson.h>

#define THERMISTOR_PIN 2
const float seriesResistor = 10000.0;
const float nominalResistance = 10000.0;
const float nominalTemperature = 25.0;
const float bCoefficient = 3950.0;
const int adcMax = 4095;
bool useNTC = true;

#ifndef GITHUB_BASE_URL
#define GITHUB_BASE_URL "https://mareknap.github.io/SIKO_teplomer/Pictures/"
#endif

float readNTCTemperature() {
  int analogValue = analogRead(THERMISTOR_PIN);
  const float vRef = 3.3;
  float voltage = ((float)analogValue / adcMax) * vRef;
  float resistance = (voltage * seriesResistor) / (vRef - voltage);
  if (resistance <= 0) {
    Serial.println("[NTC] Warning: invalid resistance");
    return -999.0;
  }
  float steinhart;
  steinhart = resistance / nominalResistance;
  steinhart = log(steinhart);
  steinhart /= bCoefficient;
  steinhart += 1.0 / (nominalTemperature + 273.15);
  steinhart = 1.0 / steinhart;
  float celsius = steinhart - 273.15;
  Serial.print("[NTC] Raw analog: "); Serial.println(analogValue);
  Serial.print("[NTC] Calculated °C: "); Serial.println(celsius);
  return celsius;
}

#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
WebServer server(80);

float lastValidTemperature = 0.0;
float temperature = 0.0;
float calibration = 0.0;
float comfortMin = 21.0;
float comfortMax = 24.0;
unsigned long lastUpdateTime = 0;
float history[192];
int historyIndex = 0;

#define EEPROM_COMFORT_MIN 0
#define EEPROM_COMFORT_MAX 4
#define EEPROM_CALIBRATION 8
#define EEPROM_SENSOR_TYPE 12

String selectMemeURL() {
  float temp = temperature;

  Serial.print("[DEBUG] Temp: "); Serial.println(temp);
  Serial.print("[DEBUG] ComfortMin: "); Serial.println(comfortMin);
  Serial.print("[DEBUG] ComfortMax: "); Serial.println(comfortMax);

  if (isnan(temp) || temp <= -999) {
    return String(GITHUB_BASE_URL) + "error.png";
  } else if (temp < (comfortMin - 2.0)) {
    return String(GITHUB_BASE_URL) + "zima_3.png";
  } else if (temp < (comfortMin - 1.0)) {
    return String(GITHUB_BASE_URL) + "zima_2.png";
  } else if (temp < comfortMin) {
    return String(GITHUB_BASE_URL) + "zima_1.png";
  } else if (temp > (comfortMax + 2.0)) {
    return String(GITHUB_BASE_URL) + "horko_3.png";
  } else if (temp > (comfortMax + 1.0)) {
    return String(GITHUB_BASE_URL) + "horko_2.png";
  } else if (temp > comfortMax) {
    return String(GITHUB_BASE_URL) + "horko_1.png";
  } else {
    return String(GITHUB_BASE_URL) + "ok_1.png";
  }
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
<meta charset="UTF-8"><title>IT Teploměr 🐥</title>
<style>
  body {
    background-color: #1e1e1e;
    color: #f0f0f0;
    font-family: Arial, sans-serif;
    text-align: center;
    margin: 0; padding: 0;
    font-size: 1.8em;
  }
  h1 {
    margin-top: 20px;
    font-size: 2.5em;
  }
  #temperature {
    font-size: 2.5em;
    color: #00d1b2;
  }
  #meme {
    max-width: 100%;
    max-height: calc(100vh - 400px);
    margin-top: 10px;
    border-radius: 10px;
  }
  #graph {
    width: calc(100% - 20px);
    height: auto;
    background: #2a2a2a;
    margin: 20px auto;
    display: block;
  }
</style>
</head><body>
<h1 style="font-size:2em; font-weight:bold;">IT Teploměr <a href='/settings' style='text-decoration:none;'>🐥</a></h1>
<div id="clock"></div>
<div id="stardate"></div>
<div>Teplota: <span id="temperature">--</span> °C</div>
<canvas id="graph" width="800" height="300"></canvas>
<img id="meme" src="" alt="Meme obrázek">

<script>
function updateClock() {
  const now = new Date();
  document.getElementById("clock").innerText = now.toLocaleString("cs-CZ");
  const stardate = (now.getFullYear() - 2000) * 1000 + now.getMonth() * 83 + now.getDate();
  document.getElementById("stardate").innerText = "Hvězdné datum: " + stardate;
}
setInterval(updateClock, 1000); updateClock();

async function updateData() {
  try {
    const t = await fetch("/api/temp").then(r => r.json());
    document.getElementById("temperature").innerText = (t.temperature !== undefined && !isNaN(t.temperature)) ? t.temperature.toFixed(1) : '--';
    const m = await fetch("/api/meme").then(r => r.json());
    document.getElementById("meme").src = m.meme;
    const h = await fetch("/api/history").then(r => r.json());
    drawChart(h);
  } catch (e) { console.error("Chyba:", e); }
}
setInterval(updateData, 5000); updateData();

function drawChart(data) {
  const c = document.getElementById("graph");
  const ctx = c.getContext("2d");
  ctx.clearRect(0, 0, c.width, c.height);

  // ✅ Získáme pouze reálná data
  const filtered = data.filter(n => n !== 0 && !isNaN(n) && n > -100);
  if (filtered.length < 2) return;

  // ✅ Vypočítáme minimální a maximální hodnoty
  const minTemp = Math.floor(Math.min(...filtered));
  const maxTemp = Math.ceil(Math.max(...filtered));
  const padding = 1;
  const yMin = minTemp - padding;
  const yMax = maxTemp + padding;
  const yRange = yMax - yMin;

  // 🎨 Kreslíme vodorovné čáry a popisky teploty (osa Y)
  ctx.strokeStyle = "#444";
  ctx.fillStyle = "#aaa";
  ctx.font = "14px Arial";
  ctx.textAlign = "left";
  const yStep = Math.ceil(yRange / 6);

  for (let y = yMin; y <= yMax; y += yStep) {
    const yPos = c.height - ((y - yMin) / yRange) * c.height;
    ctx.beginPath();
    ctx.moveTo(0, yPos);
    ctx.lineTo(c.width, yPos);
    ctx.stroke();
    ctx.fillText(y + "°C", 5, yPos - 5);
  }

  // 📈 Kreslíme teplotní křivku
  ctx.strokeStyle = "#00d1b2";
  ctx.beginPath();
  const sx = c.width / (filtered.length - 1);
  filtered.forEach((t, i) => {
    const x = i * sx;
    const y = c.height - ((t - yMin) / yRange) * c.height;
    if (!isNaN(y)) {
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
  });
  ctx.stroke();

  // 🕒 Osa X – časové značky
  const now = new Date();
  ctx.fillStyle = "#aaa";
  ctx.textAlign = "center";

  let labelStep;
  if (filtered.length <= 60) labelStep = 5;         // každých 5 sekund
  else if (filtered.length <= 720) labelStep = 60;  // každých 5 minut
  else labelStep = 180;                             // každé 3 hodiny

  for (let i = 0; i < filtered.length; i += labelStep) {
    const past = new Date(now.getTime() - (filtered.length - 1 - i) * 5 * 1000);
    let label = "";

    if (filtered.length <= 60) {
      label = past.getSeconds().toString().padStart(2, "0") + "s";
    } else if (filtered.length <= 720) {
      label = past.getHours().toString().padStart(2, "0") + ":" +
              past.getMinutes().toString().padStart(2, "0");
    } else {
      label = past.getHours().toString().padStart(2, "0") + ":00";
    }

    const x = i * sx;
    ctx.fillText(label, x, c.height - 5);
  }
}

</script></body></html>
)rawliteral";

void saveComfortToEEPROM() {
  EEPROM.write(EEPROM_SENSOR_TYPE, useNTC ? 1 : 0);
  EEPROM.put(EEPROM_COMFORT_MIN, comfortMin);
  EEPROM.put(EEPROM_COMFORT_MAX, comfortMax);
  EEPROM.put(EEPROM_CALIBRATION, calibration);
  EEPROM.commit();
}
void loadComfortFromEEPROM() {
  useNTC = EEPROM.read(EEPROM_SENSOR_TYPE) == 1;
  EEPROM.get(EEPROM_COMFORT_MIN, comfortMin);
  EEPROM.get(EEPROM_COMFORT_MAX, comfortMax);
  EEPROM.get(EEPROM_CALIBRATION, calibration);

  if (isnan(comfortMin) || comfortMin < 5 || comfortMin > 50) comfortMin = 21.0;
  if (isnan(comfortMax) || comfortMax < 5 || comfortMax > 50) comfortMax = 24.0;
  if (isnan(calibration)) calibration = 0.0;

  Serial.print("[EEPROM] comfortMin: "); Serial.println(comfortMin);
  Serial.print("[EEPROM] comfortMax: "); Serial.println(comfortMax);
  Serial.print("[EEPROM] calibration: "); Serial.println(calibration);
}

void readTemperature() {
  float rawTemp;
  if (useNTC) {
    rawTemp = readNTCTemperature();
  } else {
    sensors.requestTemperatures();
    rawTemp = sensors.getTempCByIndex(0);
  }

  if (rawTemp != DEVICE_DISCONNECTED_C && rawTemp > -100.0 && rawTemp < 100.0) {
    temperature = rawTemp + calibration;
  }
}

void handleTemp() {
  StaticJsonDocument<100> doc;
  int analogRaw = useNTC ? analogRead(THERMISTOR_PIN) : -1;
  doc["temperature"] = isnan(temperature) ? -999.0 : temperature;
  doc["calibration"] = calibration;
  doc["sensorType"] = useNTC ? "ntc" : "ds18b20";
  doc["analogRaw"] = analogRaw;
  String json; serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleHistory() {
  StaticJsonDocument<3000> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < 192; i += 2) arr.add(history[i]);
  String json; serializeJson(doc, json);
  server.send(200, "application/json", json);

}
void handleStatus() {
  int analogRaw = analogRead(THERMISTOR_PIN);
  StaticJsonDocument<200> doc;
  doc["uptime"] = millis() / 1000;
  doc["version"] = "v2.0";
  doc["comfortMin"] = comfortMin;
  doc["comfortMax"] = comfortMax;
  doc["calibration"] = calibration;
  doc["sensorType"] = useNTC ? "ntc" : "ds18b20";
  String json; serializeJson(doc, json);
  server.send(200, "application/json", json);
}
void handleMeme() {
  StaticJsonDocument<200> doc;
  doc["meme"] = selectMemeURL();
  String json; serializeJson(doc, json);
  server.send(200, "application/json", json);
}
void handleSetComfort() {
  if (server.method() == HTTP_POST) {
    StaticJsonDocument<200> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) return server.send(400, "application/json", "{\"error\":\"Bad JSON\"}");
    comfortMin = doc["comfortMin"] | comfortMin;
    comfortMax = doc["comfortMax"] | comfortMax;
    calibration = doc["calibration"] | calibration;
    String type = doc["sensorType"] | "ds18b20";
    useNTC = (type == "ntc");
    saveComfortToEEPROM();
    server.send(200, "application/json", "{\"status\":\"updated\"}");
  } else {
    server.send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
  }
}

void handleSetConfig() {
  if (server.method() == HTTP_POST) {
    StaticJsonDocument<200> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) return server.send(400, "application/json", "{\"error\":\"Bad JSON\"}");
    comfortMin = doc["comfortMin"] | comfortMin;
    comfortMax = doc["comfortMax"] | comfortMax;
    calibration = doc["calibration"] | calibration;
    String type = doc["sensorType"] | (useNTC ? "ntc" : "ds18b20");
    useNTC = (type == "ntc");
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
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi připojeno!");
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
  
  server.on("/", []() { server.send_P(200, "text/html", index_html); });
  server.on("/api/temp", handleTemp);
  server.on("/api/history", handleHistory);
  server.on("/api/status", handleStatus);
  server.on("/api/config", HTTP_POST, handleSetConfig);
  server.on("/api/meme", handleMeme);
  server.on("/api/setcomfort", handleSetComfort);
  


  
server.on("/settings", HTTP_GET, []() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Nastavení</title>
<style>
  body { background:#1e1e1e; color:#fff; font-family:sans-serif; text-align:center; }
  input, select, button { font-size:1em; padding:5px; margin:5px; }
  label { display:block; margin-top:10px; }
</style></head><body>
<h1><a href='/' style='text-decoration:none;color:#ffd700;'>🐥</a> Nastavení</h1>
<form id="settingsForm">
  <label>Comfort Min: <input type="number" step="0.1" name="comfortMin"></label>
  <label>Comfort Max: <input type="number" step="0.1" name="comfortMax"></label>
  <label>Kalibrace: <input type="number" step="0.1" name="calibration"></label>
  <label>Last Valid Temp: <input type="number" step="0.1" name="lastValidTemperature"></label>
  <label>Délka historie (hod): <select name="historyLength">
    <option value="24">24</option><option value="48">48</option></select></label>
<label>Typ čidla: <select name="sensorType"><option value="ds18b20">DS18B20</option><option value="ntc">NTC (analog)</option></select></label>
  <label>Interval záznamu (min): <select name="historyInterval">
    <option value="15">15</option><option value="30">30</option><option value="60">60</option></select></label>
  <label>OTA URL: <input type="text" name="ota_url" style="width:90%;"></label>
  <br>
  <button type="submit">💾 Uložit změny</button>
  <button type="button" onclick="clearHistory()">🗑️ Smazat historii</button>
  <button type="button" onclick="runOTA()">🔄 Spustit OTA z URL</button>
</form>
<br><hr><br>
<form method="POST" action="/update" enctype="multipart/form-data">
  <label>📂 OTA soubor (.bin): <input type="file" name="update"></label><br>
  <input type="submit" value="Nahrát a aktualizovat">
</form>
<script>
fetch('/api/config').then(r => r.json()).then(cfg => {
  for (let k in cfg) if(document.forms[0][k]) document.forms[0][k].value = cfg[k];
});
document.getElementById("settingsForm").addEventListener("submit", function(e){
  e.preventDefault();
  let data = {}; const form = e.target;
  for (let i=0; i<form.elements.length; i++) {
    let el = form.elements[i];
    if (el.name) data[el.name] = isNaN(el.value) ? el.value : parseFloat(el.value);
  }
  fetch("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(data)
  }).then(() => alert("Uloženo!"));
});
function clearHistory(){
  fetch("/api/clearhistory", { method:"POST" }).then(() => alert("Historie smazána"));
}
function runOTA(){
  fetch("/api/update", { method:"POST" }).then(() => alert("Spouštím OTA aktualizaci..."));
}
</script>
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
});

  server.begin();
}
void loop() {
  static unsigned long lastConsoleLog = 0;
  server.handleClient();
  if (millis() - lastConsoleLog > 5000) {
    float rawTemp;
    if (useNTC) {
      int analogValue = analogRead(THERMISTOR_PIN);
      Serial.print("[NTC] Raw analog: "); Serial.print(analogValue);
      rawTemp = readNTCTemperature();
    } else {
      sensors.requestTemperatures();
      rawTemp = sensors.getTempCByIndex(0);
      Serial.print("[DS18B20] Raw: "); Serial.print(rawTemp);
    }
    Serial.print(" | Calibrated: "); Serial.println(rawTemp + calibration);
    lastConsoleLog = millis();
  }
  if (millis() - lastUpdateTime > 5000) {
    readTemperature();
    history[historyIndex] = temperature;
    historyIndex = (historyIndex + 1) % 288;
    lastUpdateTime = millis();
  }
}

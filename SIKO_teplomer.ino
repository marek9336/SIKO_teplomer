#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "secrets.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <math.h>

#define THERMISTOR_PIN 2
const float seriesResistor = 10000.0;
const float nominalResistance = 10000.0;
const float nominalTemperature = 25.0;
const float bCoefficient = 3950.0;
const int adcMax = 4095;
bool useNTC = true;

#ifndef GITHUB_BASE_URL
#define GITHUB_BASE_URL "https://raw.githubusercontent.com/marek9336/SIKO_teplomer/refs/heads/main/Pictures/"
#endif

// --- Nové: URL pro ceny a citace ---
const char* COINGECKO_URL = "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd,czk";
const char* CITACE_URL    = "https://raw.githubusercontent.com/marek9336/SIKO_teplomer/refs/heads/main/citace.txt";

float readNTCTemperature() {
  int analogValue = analogRead(THERMISTOR_PIN);
  const float vRef = 3.3;
  float voltage = ((float)analogValue / adcMax) * vRef;
  float resistance = (voltage * seriesResistor) / (vRef - voltage);
  if (resistance <= 0) {
    Serial.println("[NTC] Varování, chyba odporu");
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
  Serial.print("[NTC] Vypočítáno °C: "); Serial.println(celsius);
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

// --- Historie (oprava na 288) ---
#define HISTORY_SIZE 288
float history[HISTORY_SIZE];
int historyIndex = 0;

// --- Průměrování 5 měření ---
float avgBuf[5];
int   avgCount = 0;

// --- Cache pro BTC & citace (aby se to zbytečně nevolalo moc často) ---
unsigned long lastBTCFetch = 0;
float btcUSD = NAN, btcCZK = NAN;

unsigned long lastQuoteFetch = 0;
String cachedQuote = "";

// --- EEPROM ---
#define EEPROM_COMFORT_MIN 0
#define EEPROM_COMFORT_MAX 4
#define EEPROM_CALIBRATION 8
#define EEPROM_SENSOR_TYPE 12

String selectMemeURL() {
  float temp = temperature;

  Serial.print("Teplota: "); Serial.println(temp);

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

// --- HTML ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
<meta charset="UTF-8"><title>IT teploměr 🐥</title>
<style>
  body {
    background-color: #1e1e1e;
    color: #f0f0f0;
    font-family: Arial, sans-serif;
    text-align: center;
    margin: 0; padding: 0;
    font-size: 1.8em;
  }
  h1 { margin-top: 20px; font-size: 2.5em; }
  #temperature { font-size: 2.5em; color: #00d1b2; }
  #meme {
    max-width: 100%;
    max-height: calc(100vh - 520px);
    margin-top: 10px; border-radius: 10px;
  }
  #graph {
    width: calc(100% - 20px);
    height: auto; background: #2a2a2a;
    margin: 20px auto; display: block;
  }
  #btc, #citace { margin: 8px 0; font-size: 0.9em; color:#ddd; }
</style>
</head><body>
<h1 style="font-size:2em; font-weight:bold;">IT teploměr <a href='/settings' style='text-decoration:none;'>🐥</a></h1>
<div id="stardate"></div>
<div id="clock"></div>
<div id="internetTime"></div>
<div id="obed"></div>
<div>Teplota: <span id="temperature">--</span> °C</div>
<canvas id="graph" width="800" height="300"></canvas>

<img id="meme" src="" alt="Meme obrázek">

<div id="btc">BTC: <span id="btc_usd">--</span> USD <br> <span id="btc_czk">--</span> CZK</div>
<div id="citace">„…načítám citaci…“</div>

<div id="countdown" style="margin: 8px 0; font-size: 0.9em; color:#ddd;">
  ⏳ Odpočet do zahájení voleb: …
</div>


<script>
function updateClock() {
  const now = new Date();
  document.getElementById("clock").innerText = now.toLocaleString("cs-CZ");
  const stardate = (now.getFullYear() - 2000) * 1000 + now.getMonth() * 83 + now.getDate();
  document.getElementById("stardate").innerText = "Hvězdné datum: " + stardate;
  const utc = now.getUTCHours() + now.getUTCMinutes() / 60 + now.getUTCSeconds() / 3600;
  const bmt = utc + 1;
  const beat = Math.floor((bmt * 1000) / 24) % 1000;
  document.getElementById("internetTime").innerText = "Internetový čas: " + beat.toString().padStart(3, '0') + " beatů";
  document.getElementById("obed").innerText = "Oběd je ve 485 beatech";
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

async function updateBTC() {
  try {
    const p = await fetch("/api/btc").then(r => r.json());
    if (p && p.usd && p.czk) {
      document.getElementById("btc_usd").innerText = Number(p.usd).toLocaleString("en-US", {maximumFractionDigits:0});
      document.getElementById("btc_czk").innerText = Number(p.czk).toLocaleString("cs-CZ", {maximumFractionDigits:0});
    }
  } catch(e){ console.error(e); }
}
setInterval(updateBTC, 3600000); updateBTC(); // 1 hodina

async function updateCitace() {
  try {
    const res = await fetch("/api/citace").then(r => r.json());
    if (res && res.quote) {
      document.getElementById("citace").innerText = "„" + res.quote + "“";
    }
  } catch(e){ console.error(e); }
}
setInterval(updateCitace, 3600000); updateCitace(); // 1 hodina

function drawChart(data) {
  const c = document.getElementById("graph");
  const ctx = c.getContext("2d");
  ctx.clearRect(0, 0, c.width, c.height);

  const filtered = data.filter(n => n !== 0 && !isNaN(n) && n > -100);
  if (filtered.length < 2) return;

  const minTemp = Math.floor(Math.min(...filtered));
  const maxTemp = Math.ceil(Math.max(...filtered));
  const padding = 1;
  const yMin = minTemp - padding;
  const yMax = maxTemp + padding;
  const yRange = yMax - yMin;

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

  const now = new Date();
  ctx.fillStyle = "#aaa";
  ctx.textAlign = "center";

  let labelStep;
  if (filtered.length <= 60) labelStep = 5;
  else if (filtered.length <= 720) labelStep = 60;
  else labelStep = 180;

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

// Countdown do startu voleb do PS PČR: 3. 10. 2025 14:00 CEST
// Robustně v UTC: 14:00 CEST = 12:00 UTC (měsíce 0-index → říjen = 9)
const electionStartUTC = new Date(Date.UTC(2025, 9, 3, 12, 0, 0));

function formatDelta(ms) {
  if (ms <= 0) return "0 dní 00:00:00";
  const totalSec = Math.floor(ms / 1000);
  const days = Math.floor(totalSec / 86400);
  const rem  = totalSec % 86400;
  const hh   = String(Math.floor(rem / 3600)).padStart(2, "0");
  const mm   = String(Math.floor((rem % 3600) / 60)).padStart(2, "0");
  const ss   = String(rem % 60).padStart(2, "0");
  return `${days} dní ${hh}:${mm}:${ss}`;
}

function updateCountdown() {
  const now = new Date();
  const diff = electionStartUTC.getTime() - now.getTime();
  const el = document.getElementById("countdown");

  if (!el) return;

  if (diff > 0) {
    el.textContent = "⏳ Odpočet do zahájení voleb: " + formatDelta(diff);
  } else {
    // Volby už začaly – volitelně můžeš zobrazit jiný stav (např. „Probíhají“ / „Po volbách“)
    el.textContent = "🗳️ Volby právě probíhají nebo už začaly.";
  }
}
setInterval(updateCountdown, 1000);
updateCountdown();

</script></body></html>
)rawliteral";

// --- EEPROM ---
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

// --- Čtení teploty ---
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

// --- Helper: kruhové přidání do historie (chronologicky) ---
void pushHistory(float v) {
  history[historyIndex] = v;
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;
}

// --- API handlery ---
void handleTemp() {
  StaticJsonDocument<160> doc;
  int analogRaw = useNTC ? analogRead(THERMISTOR_PIN) : -1;
  doc["temperature"] = isnan(temperature) ? -999.0 : temperature;
  doc["calibration"] = calibration;
  doc["sensorType"] = useNTC ? "ntc" : "ds18b20";
  doc["analogRaw"] = analogRaw;
  String json; serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleHistory() {
  StaticJsonDocument<4096> doc;
  JsonArray arr = doc.to<JsonArray>();
  // Vrátíme chronologicky: nejstarší → nejnovější
  for (int i = 0; i < HISTORY_SIZE; i++) {
    int idx = (historyIndex + i) % HISTORY_SIZE;
    arr.add(history[idx]);
  }
  String json; serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleStatus() {
  StaticJsonDocument<240> doc;
  doc["uptime"] = millis() / 1000;
  doc["version"] = "v2.3-btc-citace";
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

// --- HTTPS fetch helper (bez certifikátu pro jednoduchost) ---
bool httpsGET(const char* url, String &payload) {
  WiFiClientSecure client;
  client.setTimeout(12000);
  client.setInsecure(); // pokud chceš pevný cert, můžeme doplnit root CA
  HTTPClient https;
  if (!https.begin(client, url)) return false;
  int code = https.GET();
  if (code == 200) {
    payload = https.getString();
    https.end();
    return true;
  }
  https.end();
  return false;
}

// --- BTC ceny (cache 60 s) ---
void handleBTC() {
  unsigned long now = millis();
  if (now - lastBTCFetch > 3600000 || isnan(btcUSD) || isnan(btcCZK)) {
    String body;
    if (httpsGET(COINGECKO_URL, body)) {
      StaticJsonDocument<512> doc;
      DeserializationError err = deserializeJson(doc, body);
      if (!err) {
        btcUSD = doc["bitcoin"]["usd"] | NAN;
        btcCZK = doc["bitcoin"]["czk"] | NAN;
        lastBTCFetch = now;
      }
    }
  }
  StaticJsonDocument<128> out;
  if (!isnan(btcUSD)) out["usd"] = btcUSD;
  if (!isnan(btcCZK)) out["czk"] = btcCZK;
  out["age_ms"] = (int)(millis() - lastBTCFetch);
  String json; serializeJson(out, json);
  server.send(200, "application/json", json);
}

// --- Citace (cache 60 s; náhodná řádka) ---
void handleCitace() {
  unsigned long now = millis();
  if (now - lastQuoteFetch > 3600000 || cachedQuote.length() == 0) {
    String txt;
    if (httpsGET(CITACE_URL, txt)) {
      // Normalizace konců řádků na '\n'
      txt.replace("\r\n", "\n");
      txt.replace('\r', '\n');

      int n = txt.length();
      // Spočítat počet řádků (počet \n + 1 pokud není prázdné)
      int lines = 0;
      if (n > 0) {
        lines = 1;
        for (int i = 0; i < n; i++) if (txt[i] == '\n') lines++;
      }

      if (lines <= 0) {
        cachedQuote = "(soubor prázdný)";
      } else {
        // Náhodný výběr řádku
        randomSeed(esp_random() ^ now);
        int pick = random(0, lines);

        int lineIdx = 0;
        int prev = 0;
        for (int i = 0; i <= n; i++) {
          if (i == n || txt[i] == '\n') {
            if (lineIdx == pick) {
              String line = txt.substring(prev, i);
              line.trim();
              if (line.length() == 0) line = "(prázdná řádka)";
              cachedQuote = line;
              break;
            }
            lineIdx++;
            prev = i + 1;
          }
        }
      }
      lastQuoteFetch = now;
    }
  }

  StaticJsonDocument<256> out;
  out["quote"] = cachedQuote.length() ? cachedQuote : String("Bez citace");
  String json; serializeJson(out, json);
  server.send(200, "application/json", json);
}


void setup() {
  Serial.begin(115200);
  EEPROM.begin(16);
  sensors.begin();
  loadComfortFromEEPROM();
  readTemperature();

  WiFi.setHostname("ESP32_Temp_IT");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi připojeno!");
  Serial.print("ESP MAC adresa: "); Serial.println(WiFi.macAddress());
  Serial.print("ESP hostname: "); Serial.println(WiFi.getHostname());
  Serial.print("ESP SSID: "); Serial.println(WiFi.SSID());
  Serial.print("ESP RSSI: "); Serial.println(WiFi.RSSI());
  Serial.print("ESP BSSID: "); Serial.println(WiFi.BSSIDstr());
  Serial.print("ESP channel: "); Serial.println(WiFi.channel());
  Serial.print("ComfortMin: "); Serial.println(comfortMin);
  Serial.print("ComfortMax: "); Serial.println(comfortMax);
  Serial.print("ESP IP adresa: "); Serial.println(WiFi.localIP());

  // Inicializace historie
  for (int i=0;i<HISTORY_SIZE;i++) history[i] = NAN;

  server.on("/", []() { server.send_P(200, "text/html", index_html); });
  server.on("/api/temp", handleTemp);
  server.on("/api/history", handleHistory);
  server.on("/api/status", handleStatus);
  server.on("/api/config", HTTP_POST, handleSetConfig);
  server.on("/api/meme", handleMeme);
  server.on("/api/setcomfort", handleSetComfort);

  // Nové endpointy:
  server.on("/api/btc", handleBTC);
  server.on("/api/citace", handleCitace);

  // Stávající /settings beze změny:
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
  <label>Komfort Min: <input type="number" step="0.1" name="comfortMin"></label>
  <label>Komfort Max: <input type="number" step="0.1" name="comfortMax"></label>
  <label>Kalibrace: <input type="number" step="0.1" name="calibration"></label>
  <label>Prvotní teplota (Nepoužívat): <input type="number" step="0.1" name="lastValidTemperature"></label>
  <label>Délka historie (hod): <select name="historyLength">
    <option value="24">24</option><option value="48">48</option></select></label>
  <label>Typ čidla: (Vyber vždy analog) <select name="sensorType">
    <option value="ds18b20">DS18B20</option><option value="ntc">NTC (analog)</option></select></label>
  <label>Interval záznamu (min): <select name="historyInterval">
    <option value="15">15</option><option value="30">30</option><option value="60">60</option></select></label>
  <label>OTA URL: nepoužívat<input type="text" name="ota_url" style="width:90%;"></label>
  <br>
  <button type="submit">💾 Uložit změny</button>
  <button type="button" onclick="clearHistory()">🗑️ Smazat historii</button>
  <button type="button" onclick="runOTA()">🔄 Spustit OTA z URL</button>
</form>
<br><hr><br>
<form method="POST" action="/update" enctype="multipart/form-data">
  <label> 📂 nepoužívat OTA soubor (.bin): <input type="file" name="update"></label><br>
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
  // případně přidej endpoint /api/clearhistory
  alert("Endpoint clearhistory není implementován.");
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

  // Čteme každou vteřinu
  readTemperature();

  // Uložíme do 5-průměr bufferu
  if (!isnan(temperature) && temperature > -100.0 && temperature < 100.0) {
    if (avgCount < 5) {
      avgBuf[avgCount++] = temperature;
    } else {
      // posuneme okno: šoupnout doleva (není to nejefektivnější, ale jednoduché)
      for (int i=1;i<5;i++) avgBuf[i-1] = avgBuf[i];
      avgBuf[4] = temperature;
    }
  }

  // každých ~5 s uložit průměr 5 posledních měření do historie
  if (millis() - lastUpdateTime > 5000) {
    if (avgCount > 0) {
      float sum = 0;
      for (int i=0;i<avgCount;i++) sum += avgBuf[i];
      float avg = sum / avgCount;
      pushHistory(avg);
      // vyprázdnit pro další okno
      avgCount = 0;
    } else {
      // není nová validní data → push NAN pro konzistenci
      pushHistory(NAN);
    }
    lastUpdateTime = millis();
  }

  if (millis() - lastConsoleLog > 5000) {
    float rawTemp;
    if (useNTC) {
      int analogValue = analogRead(THERMISTOR_PIN);
      Serial.print("Analogová data: "); Serial.print(analogValue);
      rawTemp = readNTCTemperature();
    } else {
      sensors.requestTemperatures();
      rawTemp = sensors.getTempCByIndex(0);
      Serial.print("DS18B20: "); Serial.print(rawTemp);
    }
    Serial.print(" | Kalibrovaná teplota: "); Serial.println(rawTemp + calibration);
    lastConsoleLog = millis();
  }

  delay(1000);
}

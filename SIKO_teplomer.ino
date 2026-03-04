#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "secrets.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <math.h>

#define FW_VERSION "1.0.4"

#define THERMISTOR_PIN 2
const float seriesResistor = 10000.0;
const float nominalResistance = 10000.0;
const float nominalTemperature = 25.0;
const float bCoefficient = 3950.0;
const int adcMax = 4095;
bool useNTC = true;

// --- 1min průměr pro zobrazení/API ---
float minuteSum = 0.0;
int minuteSamples = 0;
unsigned long lastMinuteCommit = 0;
float minuteAvgTemp = NAN;  // tohle se bude posílat přes API a zobrazovat

// --- 1h delta z minutových průměrů ---
#define MIN_HISTORY_MINUTES 180  // držíme 3 hodiny do zásoby
float minuteHistory[MIN_HISTORY_MINUTES];
int minuteHistoryIdx = 0;
bool minuteHistoryPrimed = false;  // až naplníme aspoň 60 vzorků
// --- Meme obrázky z GitHubu ---
#ifndef GITHUB_BASE_URL
#define GITHUB_BASE_URL "https://raw.githubusercontent.com/marek9336/SIKO_teplomer/refs/heads/main/Pictures/"
#endif

// --- Nové: URL pro ceny a citace ---
const char* COINGECKO_URL = "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd,czk";
const char* CITACE_URL = "https://raw.githubusercontent.com/marek9336/SIKO_teplomer/refs/heads/main/citace.txt";
// --- Odpočty (JSON z GitHubu) ---
const char* ODP_URL = "https://raw.githubusercontent.com/marek9336/SIKO_teplomer/refs/heads/main/odpocty.json";
unsigned long lastOdpoctyFetch = 0;
String odpoctyCache;
// --- NTC teploměr ---
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
  Serial.print("[NTC] Raw analog: ");
  Serial.println(analogValue);
  Serial.print("[NTC] Vypočítáno °C: ");
  Serial.println(celsius);
  return celsius;
}

#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
WebServer server(80);

float lastValidTemperature = 0.0;
float lastRawTemperature = NAN;
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
int avgCount = 0;

// --- Cache pro BTC & citace (aby se to zbytečně nevolalo moc často) ---
unsigned long lastBTCFetch = 0;
float btcUSD = NAN, btcCZK = NAN;

unsigned long lastQuoteFetch = 0;
String cachedQuote = "";
String otaLastError = "";
bool otaLastSuccess = false;
bool otaUploadStarted = false;

// --- EEPROM ---
#define EEPROM_COMFORT_MIN 0
#define EEPROM_COMFORT_MAX 4
#define EEPROM_CALIBRATION 8
#define EEPROM_SENSOR_TYPE 12

String otaErrorToString(uint8_t err) {
  switch (err) {
#ifdef UPDATE_ERROR_OK
    case UPDATE_ERROR_OK: return "OK";
#endif
#ifdef UPDATE_ERROR_WRITE
    case UPDATE_ERROR_WRITE: return "Write failed";
#endif
#ifdef UPDATE_ERROR_ERASE
    case UPDATE_ERROR_ERASE: return "Erase failed";
#endif
#ifdef UPDATE_ERROR_READ
    case UPDATE_ERROR_READ: return "Read failed";
#endif
#ifdef UPDATE_ERROR_SPACE
    case UPDATE_ERROR_SPACE: return "Not enough space";
#endif
#ifdef UPDATE_ERROR_SIZE
    case UPDATE_ERROR_SIZE: return "Invalid size";
#endif
#ifdef UPDATE_ERROR_STREAM
    case UPDATE_ERROR_STREAM: return "Stream read timeout";
#endif
#ifdef UPDATE_ERROR_MD5
    case UPDATE_ERROR_MD5: return "MD5 mismatch";
#endif
#ifdef UPDATE_ERROR_MAGIC_BYTE
    case UPDATE_ERROR_MAGIC_BYTE: return "Invalid firmware format";
#endif
#ifdef UPDATE_ERROR_ABORT
    case UPDATE_ERROR_ABORT: return "Upload aborted";
#endif
#ifdef UPDATE_ERROR_ACTIVATE
    case UPDATE_ERROR_ACTIVATE: return "Could not activate new firmware";
#endif
#ifdef UPDATE_ERROR_NO_PARTITION
    case UPDATE_ERROR_NO_PARTITION: return "No OTA partition";
#endif
#ifdef UPDATE_ERROR_BAD_ARGUMENT
    case UPDATE_ERROR_BAD_ARGUMENT: return "Bad argument";
#endif
#ifdef UPDATE_ERROR_VALIDATE_FAILED
    case UPDATE_ERROR_VALIDATE_FAILED: return "Validation failed";
#endif
    default: return String("Unknown error code ") + String(err);
  }
}

String selectMemeURL() {
  float temp = minuteAvgTemp;

  Serial.print("Teplota: ");
  Serial.println(temp);

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
  #countdownEvents { white-space: pre-line; }
</style>
</head><body>
<h1 style="font-size:2em; font-weight:bold;">IT teploměr <a href='/settings' style='text-decoration:none;'>🐥</a></h1>
<div id="stardate"></div>
<div id="clock"></div>
<div id="internetTime"></div>
<div id="lunchBeats"></div>
<div id="obed"></div>
<div>Teplota: <span id="temperature">--</span> °C</div>
<div id="delta" style="font-size:0.9em;color:#ddd;">Δ poslední hodina: -- °C</div>
<!--<canvas id="graph" width="800" height="300"></canvas>-->

<img id="meme" src="" alt="Meme obrázek">

<div id="btc">₿TC: <span id="btc_usd">--</span> USD <br> <span id="btc_czk">--</span> CZK</div>
<div id="citace">„…načítám citaci…“</div>

<div id="countdown" style="margin: 8px 0; font-size: 0.9em; color:#ddd;">
  Volby budou za: …
</div>
<div id="countdownEvents" style="margin: 8px 0; font-size: 0.9em; color:#ddd;">
  🗓️ Načítám události…
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
  document.getElementById("lunchBeats").innerText = "Oběd je ve 485 beatech";
}
setInterval(updateClock, 1000); updateClock();

async function updateData() {
  try {
    const t = await fetch("/api/temp").then(r => r.json());
    // minutový průměr
    const temp = (t.temperature !== undefined && !isNaN(t.temperature)) ? Number(t.temperature) : NaN;
    document.getElementById("temperature").innerText = isNaN(temp) ? '--' : temp.toFixed(1);

    // delta 1h
    const d = (t.delta1h !== undefined && !isNaN(t.delta1h)) ? Number(t.delta1h) : NaN;
    const sign = isNaN(d) ? "" : (d > 0 ? "+" : "");
    document.getElementById("delta").innerText = "Δ poslední hodina: " + (isNaN(d) ? "--" : (sign + d.toFixed(1))) + " °C";

    // meme
    const m = await fetch("/api/meme").then(r => r.json());
    document.getElementById("meme").src = m.meme;

  } catch (e) { console.error("Chyba:", e); }
}
setInterval(updateData, 5000); updateData();  // klidně nech 5 s, mění se jen když /api/temp “commitne” minutu

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
    el.textContent = "Odpočet do zahájení voleb: " + formatDelta(diff);
  } else {
    // Volby už začaly – volitelně můžeš zobrazit jiný stav (např. „Probíhají“ / „Po volbách“)
    el.textContent = "🗳️ Volby právě probíhají nebo už začaly.";
  }
}
setInterval(updateCountdown, 1000);
updateCountdown();

// Odpočet do oběda (každý den 11:40 místního času)
function nextLunchTarget(now) {
  const target = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 11, 40, 0, 0);
  return (now >= target) ? new Date(target.getTime() + 24*3600*1000) : target;
}
function fmtHMS(ms) {
  if (ms <= 0) return "00:00:00";
  const totalSec = Math.floor(ms / 1000);
  const hh = String(Math.floor(totalSec / 3600)).padStart(2,"0");
  const mm = String(Math.floor((totalSec % 3600) / 60)).padStart(2,"0");
  const ss = String(totalSec % 60).padStart(2,"0");
  return `${hh}:${mm}:${ss}`;
}
function fmtDHMS(ms) {
  if (ms <= 0) return "00:00:00";
  const totalSec = Math.floor(ms / 1000);
  const days = Math.floor(totalSec / 86400);
  const rem  = totalSec % 86400;
  const hh   = String(Math.floor(rem / 3600)).padStart(2,"0");
  const mm   = String(Math.floor((rem % 3600) / 60)).padStart(2,"0");
  const ss   = String(rem % 60).padStart(2,"0");
  if (days > 0) {
    const dWord = (days === 1 ? "den" : (days >= 2 && days <= 4 ? "dny" : "dní"));
    return `${days} ${dWord} ${hh}:${mm}:${ss}`;
  }
  return `${hh}:${mm}:${ss}`;
}
function updateLunchCountdown() {
  const now = new Date();
  const t = nextLunchTarget(now);
  const diff = t.getTime() - now.getTime();
  const el = document.getElementById("obed");
  if (el) el.textContent = "Oběd bude za: " + fmtHMS(diff);
}
setInterval(updateLunchCountdown, 1000);
updateLunchCountdown();
let countdownList = [];
let currentEvent = null;
let currentStartMs = null;
let currentEndMs = null;

// Parse začátku: podporujeme víc klíčů a formátů (ISO s 'Z' / +offset i číslo epoch)
function parseStartMs(item) {
  if (!item) return NaN;
  // preferované klíče
  const keys = ["start", "start_utc", "startISO", "datetime", "time", "iso", "ts", "epoch"];
  let v;
  for (const k of keys) {
    if (item[k] !== undefined) { v = item[k]; break; }
  }
  if (v === undefined) return NaN;
  if (typeof v === "number") return (v > 1e12 ? v : v * 1000); // epoch ms/s
  const d = new Date(v);
  if (!isNaN(d.getTime())) return d.getTime();          // ISO string
  // fallback: pokud někdo dá epoch v sekundách jako string
  const n = Number(v);
  if (!isNaN(n)) return (n > 1e12 ? n : n * 1000);
  return NaN;
}

function parseEndMs(item) {
  if (!item) return NaN;
  if (item.end !== undefined) {
    if (typeof item.end === "number") return item.end;
    const d = new Date(item.end);
    if (!isNaN(d.getTime())) return d.getTime();
  }
  if (item.durationMinutes !== undefined) {
    const start = parseStartMs(item);
    if (!isNaN(start)) return start + Number(item.durationMinutes) * 60000;
  }
  return NaN; // není-li definováno, bereme jen start
}

function normalizeList(raw) {
  // JSON může být pole, nebo {events:[...]}
  if (Array.isArray(raw)) return raw;
  if (raw && Array.isArray(raw.events)) return raw.events;
  return [];
}

// vyber nejbližší budoucí
function pickNextEvent(list) {
  const now = Date.now();
  let best = null;
  let bestStart = Infinity;
  for (const it of list) {
    const s = parseStartMs(it);
    if (!isNaN(s) && s > now && s < bestStart) {
      best = it; bestStart = s;
    }
  }
  if (best) {
    currentEvent = best;
    currentStartMs = parseStartMs(best);
    currentEndMs = parseEndMs(best);
    return true;
  }
  return false;
}

function getUpcomingEvents(list, nowMs) {
  const out = [];
  for (const it of list) {
    const s = parseStartMs(it);
    if (!isNaN(s) && s > nowMs) out.push({ item: it, startMs: s });
  }
  out.sort((a, b) => a.startMs - b.startMs);
  return out;
}

function renderGenericCountdown() {
  const el = document.getElementById("countdownEvents");
  if (!el) return;
  if ((!currentEvent || !currentStartMs) && !pickNextEvent(countdownList)) {
    el.textContent = "🗓️ Žádná příští událost";
    return;
  }

  const now = Date.now();
  const diff = currentStartMs - now;
  let firstLine = "";
  const title = currentEvent.title || "Událost";
  if (diff > 0) {
    firstLine = `⏳ ${title}: ${fmtDHMS(diff)}`;
  } else {
    // po začátku: pokud je definovaný konec, ukaz 'Probíhá'; jinak přejdi na další
    if (!isNaN(currentEndMs) && now < currentEndMs) {
      firstLine = `🟢 ${title}: probíhá`;
    } else {
      // vyber další z již nacachovaného listu, ať to hned přepne
      if (!pickNextEvent(countdownList)) {
        el.textContent = "🗓️ Žádná příští událost";
        return;
      }
      renderGenericCountdown();
      return;
    }
  }

  const lines = [firstLine];
  const cutoff = (currentStartMs > now) ? currentStartMs : now;
  const nextTwo = getUpcomingEvents(countdownList, now)
    .filter(e => e.startMs > cutoff)
    .slice(0, 2);

  for (const ev of nextTwo) {
    const t = ev.item.title || "Událost";
    lines.push(`➡️ ${t}: ${fmtDHMS(ev.startMs - now)}`);
  }
  el.textContent = lines.join("\n");
}

async function loadCountdowns() {
  try {
    // proxy přes ESP kvůli CORS + máme 1h cache na backendu
    const data = await fetch("/api/odpocty?ts=" + Date.now()).then(r => r.json());
    countdownList = normalizeList(data);

    // seřadit pro jistotu podle startu
    countdownList.sort((a,b) => parseStartMs(a) - parseStartMs(b));

    // vyber nejbližší
    pickNextEvent(countdownList);
  } catch(e) {
    console.error("odpocty load fail:", e);
  }
}

// refresh UI každou sekundu, data každou hodinu (stejný rytmus jako BTC/citace)
setInterval(renderGenericCountdown, 1000);
setInterval(loadCountdowns, 3600000);
loadCountdowns();
renderGenericCountdown();

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

  Serial.print("[EEPROM] comfortMin: ");
  Serial.println(comfortMin);
  Serial.print("[EEPROM] comfortMax: ");
  Serial.println(comfortMax);
  Serial.print("[EEPROM] calibration: ");
  Serial.println(calibration);
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
    lastRawTemperature = rawTemp;
    lastValidTemperature = rawTemp + calibration;
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
  StaticJsonDocument<192> doc;
  int analogRaw = useNTC ? analogRead(THERMISTOR_PIN) : -1;
  doc["temperature"] = isnan(minuteAvgTemp) ? -999.0 : minuteAvgTemp;  // 1min avg
  doc["delta1h"] = getDelta1h();                                       // může být NaN
  doc["calibration"] = calibration;
  doc["sensorType"] = useNTC ? "ntc" : "ds18b20";
  doc["analogRaw"] = analogRaw;
  String json;
  serializeJson(doc, json);
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
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleStatus() {
  StaticJsonDocument<240> doc;
  doc["uptime"] = millis() / 1000;
  doc["version"] = FW_VERSION;
  doc["comfortMin"] = comfortMin;
  doc["comfortMax"] = comfortMax;
  doc["calibration"] = calibration;
  doc["sensorType"] = useNTC ? "ntc" : "ds18b20";
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

void handleGetConfig() {
  StaticJsonDocument<256> doc;
  doc["comfortMin"] = comfortMin;
  doc["comfortMax"] = comfortMax;
  doc["calibration"] = calibration;
  doc["sensorType"] = useNTC ? "ntc" : "ds18b20";
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleClearHistory() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
    return;
  }

  for (int i = 0; i < HISTORY_SIZE; i++) history[i] = NAN;
  historyIndex = 0;

  for (int i = 0; i < MIN_HISTORY_MINUTES; i++) minuteHistory[i] = NAN;
  minuteHistoryIdx = 0;
  minuteSum = 0.0;
  minuteSamples = 0;
  minuteAvgTemp = temperature;
  lastMinuteCommit = millis();

  server.send(200, "application/json", "{\"status\":\"history_cleared\"}");
}

void pushMinuteHistory(float v) {
  minuteHistory[minuteHistoryIdx] = v;
  minuteHistoryIdx = (minuteHistoryIdx + 1) % MIN_HISTORY_MINUTES;
}

float getDelta1h() {
  // potřebujeme 60 minut starý vzorek
  const int back = 60;
  // zjisti, kolik platných vzorků máme
  int valid = 0;
  for (int i = 0; i < MIN_HISTORY_MINUTES; i++)
    if (!isnan(minuteHistory[i])) valid++;
  if (valid < back + 1) return NAN;

  int idxPast = (minuteHistoryIdx - back - 1 + MIN_HISTORY_MINUTES) % MIN_HISTORY_MINUTES;
  float past = minuteHistory[idxPast];
  if (isnan(past) || isnan(minuteAvgTemp)) return NAN;
  return minuteAvgTemp - past;
}

// --- HTTPS fetch helper (bez certifikátu pro jednoduchost) ---
bool httpsGET(const char* url, String& payload) {
  WiFiClientSecure client;
  client.setTimeout(12000);
  client.setInsecure();  // pokud chceš pevný cert, můžeme doplnit root CA
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
  String json;
  serializeJson(out, json);
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
        for (int i = 0; i < n; i++)
          if (txt[i] == '\n') lines++;
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
  String json;
  serializeJson(out, json);
  server.send(200, "application/json", json);
}

void handleOdpocty() {
  unsigned long now = millis();
  // Cache 1 hodina (stejně jako BTC/citace)
  if (now - lastOdpoctyFetch > 3600000UL || odpoctyCache.length() == 0) {
    String body;
    if (httpsGET(ODP_URL, body) && body.length() > 0) {
      odpoctyCache = body;
      lastOdpoctyFetch = now;
    }
  }
  if (odpoctyCache.length() == 0) {
    server.send(502, "application/json", "{\"error\":\"fetch_failed\"}");
  } else {
    server.send(200, "application/json", odpoctyCache);
  }
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(16);
  sensors.begin();
  loadComfortFromEEPROM();
  readTemperature();
  for (int i = 0; i < MIN_HISTORY_MINUTES; i++) minuteHistory[i] = NAN;
  minuteAvgTemp = temperature;  // do prvního commit-u ukaž aktuální
  lastMinuteCommit = millis();  // start 1min okna

  WiFi.setHostname("ESP32_Temp_IT");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi připojeno!");
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
  Serial.print("ComfortMin: ");
  Serial.println(comfortMin);
  Serial.print("ComfortMax: ");
  Serial.println(comfortMax);
  Serial.print("ESP IP adresa: ");
  Serial.println(WiFi.localIP());

  // Inicializace historie
  for (int i = 0; i < HISTORY_SIZE; i++) history[i] = NAN;

  server.on("/", []() {
    server.send_P(200, "text/html", index_html);
  });
  server.on("/api/temp", handleTemp);
  server.on("/api/history", handleHistory);
  server.on("/api/status", handleStatus);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handleSetConfig);
  server.on("/api/clearhistory", HTTP_POST, handleClearHistory);
  server.on("/api/meme", handleMeme);
  server.on("/api/setcomfort", handleSetComfort);
  server.on("/api/odpocty", handleOdpocty);
  server.on(
    "/update", HTTP_POST,
    []() {
      bool ok = otaUploadStarted && otaLastSuccess && otaLastError.length() == 0;
      StaticJsonDocument<256> out;
      out["ok"] = ok;
      out["version"] = FW_VERSION;
      if (ok) {
        out["message"] = "OTA OK, restartuji...";
      } else {
        if (otaLastError.length() == 0) otaLastError = "Update finalize failed";
        out["message"] = "OTA FAIL";
        out["error"] = otaLastError;
        out["error_code"] = (int)Update.getError();
      }
      String response;
      serializeJson(out, response);
      server.sendHeader("Connection", "close");
      server.send(ok ? 200 : 500, "application/json", response);
      delay(500);
      if (ok) ESP.restart();
    },
    []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        otaLastError = "";
        otaLastSuccess = false;
        otaUploadStarted = true;
        if (upload.totalSize == 0) {
          otaLastError = "Empty upload";
          return;
        }
        if (!Update.begin(upload.totalSize, U_FLASH)) {
          otaLastError = otaErrorToString(Update.getError());
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          otaLastError = otaErrorToString(Update.getError());
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        otaLastError = "Upload aborted by client";
        otaLastSuccess = false;
        Update.abort();
      } else if (upload.status == UPLOAD_FILE_END) {
        if (!Update.end()) {
          otaLastError = otaErrorToString(Update.getError());
          otaLastSuccess = false;
          Update.printError(Serial);
        } else {
          otaLastSuccess = true;
        }
      }
    });

  // Nové endpointy:
  server.on("/api/btc", handleBTC);
  server.on("/api/citace", handleCitace);

  // /settings
  server.on("/settings", HTTP_GET, []() {
    String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Nastavení</title>
<style>
  body { background:#161616; color:#fff; font-family:Segoe UI,Arial,sans-serif; margin:0; padding:16px; }
  .wrap { max-width:760px; margin:0 auto; }
  .card { background:#222; border:1px solid #343434; border-radius:12px; padding:14px; margin:12px 0; }
  .title { font-size:1.3em; margin:0 0 8px; color:#ffd700; }
  .version { font-size:0.95em; color:#cfcfcf; margin-bottom:8px; }
  .row { display:block; margin:8px 0; }
  label { display:block; margin-bottom:4px; color:#ddd; }
  input, select, button { font-size:1em; padding:8px; margin:4px 0; border-radius:8px; border:1px solid #444; background:#111; color:#fff; }
  input[type=number], select { width:100%; max-width:320px; }
  button { cursor:pointer; background:#2c3e50; }
  button:hover { background:#34506a; }
  .danger { background:#5a2a2a; }
  .ota-actions { display:flex; gap:8px; flex-wrap:wrap; align-items:center; }
  .progress-wrap { margin-top:8px; }
  progress { width:100%; height:18px; }
  #otaLog { background:#0f0f0f; color:#b9f7b9; border:1px solid #333; border-radius:8px; padding:8px; min-height:80px; white-space:pre-wrap; overflow-wrap:anywhere; }
  .muted { color:#aaa; font-size:0.9em; }
</style></head><body>
<div class="wrap">
  <h1 class="title"><a href='/' style='text-decoration:none;color:#ffd700;'>🐥</a> Nastavení</h1>
  <div class="version">Aktuální verze: <strong id="fwVersion">1.0.4</strong></div>

  <div class="card">
    <h2 class="title">Konfigurace měření</h2>
    <form id="settingsForm">
      <div class="row">
        <label>Komfort Min</label>
        <input type="number" step="0.1" name="comfortMin">
      </div>
      <div class="row">
        <label>Komfort Max</label>
        <input type="number" step="0.1" name="comfortMax">
      </div>
      <div class="row">
        <label>Kalibrace</label>
        <input type="number" step="0.1" name="calibration">
      </div>
      <div class="row">
        <label>Typ čidla (doporučeno analog/NTC)</label>
        <select name="sensorType">
          <option value="ds18b20">DS18B20</option>
          <option value="ntc">NTC (analog)</option>
        </select>
      </div>
      <div class="ota-actions">
        <button type="submit">💾 Uložit změny</button>
        <button type="button" class="danger" onclick="clearHistory()">🗑️ Smazat historii</button>
      </div>
    </form>
  </div>

  <div class="card">
    <h2 class="title">OTA aktualizace</h2>
    <form id="otaForm" enctype="multipart/form-data">
      <div class="row">
        <label>Firmware soubor (.bin)</label>
        <input id="otaFile" type="file" name="update" accept=".bin" required>
      </div>
      <div class="ota-actions">
        <button id="otaBtn" type="submit">⬆️ Nahrát firmware</button>
      </div>
      <div class="progress-wrap">
        <progress id="otaProgress" value="0" max="100"></progress>
        <div class="muted" id="otaProgressText">Připraveno</div>
      </div>
    </form>
    <h3 class="title" style="font-size:1.05em;">OTA log</h3>
    <pre id="otaLog">Čekám na soubor...</pre>
  </div>
</div>
<script>
fetch('/api/config').then(r => r.json()).then(cfg => {
  for (let k in cfg) if(document.forms[0][k]) document.forms[0][k].value = cfg[k];
}).catch(() => {});
fetch('/api/status').then(r => r.json()).then(s => {
  if (s && s.version) document.getElementById('fwVersion').textContent = s.version;
}).catch(() => {});

document.getElementById("settingsForm").addEventListener("submit", function(e){
  e.preventDefault();
  let data = {}; const form = e.target;
  for (let i=0; i<form.elements.length; i++) {
    let el = form.elements[i];
    if (!el.name) continue;
    if (el.tagName === "SELECT") {
      data[el.name] = el.value;
      continue;
    }
    if (el.type === "number") {
      const raw = (el.value || "").trim();
      if (!raw) continue;
      const n = Number(raw);
      if (Number.isFinite(n)) data[el.name] = n;
      continue;
    }
    const raw = (el.value || "").trim();
    if (raw) data[el.name] = raw;
  }
  fetch("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(data)
  }).then(() => alert("Uloženo!"));
});

function clearHistory(){
  fetch("/api/clearhistory", { method:"POST" })
    .then(r => r.json())
    .then(() => alert("Historie smazána."))
    .catch(() => alert("Chyba při mazání historie."));
}

const otaForm = document.getElementById("otaForm");
const otaProgress = document.getElementById("otaProgress");
const otaProgressText = document.getElementById("otaProgressText");
const otaLog = document.getElementById("otaLog");
const otaBtn = document.getElementById("otaBtn");

function logOTA(msg){
  otaLog.textContent += "\n" + msg;
  otaLog.scrollTop = otaLog.scrollHeight;
}

otaForm.addEventListener("submit", function(e){
  e.preventDefault();
  const fileInput = document.getElementById("otaFile");
  if (!fileInput.files || !fileInput.files[0]) {
    alert("Vyber .bin soubor.");
    return;
  }
  const file = fileInput.files[0];
  otaProgress.value = 0;
  otaProgressText.textContent = "Start uploadu...";
  otaLog.textContent = "Vybraný soubor: " + file.name + " (" + file.size + " B)";
  otaBtn.disabled = true;

  const formData = new FormData();
  formData.append("update", file);

  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/update", true);

  xhr.upload.onprogress = function(ev){
    if (ev.lengthComputable) {
      const pct = Math.round((ev.loaded / ev.total) * 100);
      otaProgress.value = pct;
      otaProgressText.textContent = "Nahrávání: " + pct + "%";
    }
  };

  xhr.onload = function(){
    otaBtn.disabled = false;
    let resp = {};
    try { resp = JSON.parse(xhr.responseText || "{}"); } catch(_e){}
    if (xhr.status >= 200 && xhr.status < 300 && resp.ok) {
      otaProgress.value = 100;
      otaProgressText.textContent = "Hotovo, zařízení se restartuje";
      logOTA("OTA OK: " + (resp.message || "restart"));
    } else {
      otaProgressText.textContent = "OTA selhalo";
      logOTA("OTA FAIL: " + (resp.error || resp.message || "neznámá chyba"));
      if (resp.error_code !== undefined) logOTA("Kód chyby: " + resp.error_code);
    }
  };

  xhr.onerror = function(){
    otaBtn.disabled = false;
    otaProgressText.textContent = "Chyba spojení";
    logOTA("Síťová chyba během uploadu.");
  };

  xhr.send(formData);
});
</script>
</body></html>
)rawliteral";
    server.send(200, "text/html", html);
  });

  server.begin();
}

void loop() {
  static unsigned long lastSampleAt = 0;
  static unsigned long lastConsoleLog = 0;
  unsigned long now = millis();
  server.handleClient();

  // Čtení jednou za sekundu bez blokování celé smyčky
  if (now - lastSampleAt >= 1000UL) {
    lastSampleAt = now;
    readTemperature();

    if (!isnan(temperature) && temperature > -100.0 && temperature < 100.0) {
      minuteSum += temperature;
      minuteSamples++;

      if (avgCount < 5) {
        avgBuf[avgCount++] = temperature;
      } else {
        for (int i = 1; i < 5; i++) avgBuf[i - 1] = avgBuf[i];
        avgBuf[4] = temperature;
      }
    }
  }

  // každých 60 s zveřejni průměr poslední minuty
  if (millis() - lastMinuteCommit >= 60000UL) {
    if (minuteSamples > 0) {
      minuteAvgTemp = minuteSum / minuteSamples;
    }
    // push do minutové historie pro Δ1h
    pushMinuteHistory(minuteAvgTemp);

    // reset okna
    minuteSum = 0.0;
    minuteSamples = 0;
    lastMinuteCommit = millis();
  }

  // každých ~5 s uložit průměr 5 posledních měření do historie
  if (millis() - lastUpdateTime > 5000) {
    if (avgCount > 0) {
      float sum = 0;
      for (int i = 0; i < avgCount; i++) sum += avgBuf[i];
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
    if (useNTC) {
      int analogValue = analogRead(THERMISTOR_PIN);
      Serial.print("Analogová data: ");
      Serial.print(analogValue);
      Serial.print(" | NTC raw: ");
      Serial.print(lastRawTemperature);
    } else {
      Serial.print("DS18B20 raw: ");
      Serial.print(lastRawTemperature);
    }
    Serial.print(" | Kalibrovaná teplota: ");
    Serial.println(temperature);
    lastConsoleLog = millis();
  }

  delay(2);
}


# 🐥 SIKO Teploměr (ESP32 + DS18B20 + OTA + Web UI)

## 🔥 Funkce:
- Měření teploty pomocí DS18B20 (GPIO 4)
- HTML UI: hodiny, hvězdné datum, graf 24h, meme obrázky
- REST API:
  - `/api/temp`, `/api/history`, `/api/status`, `/api/meme`
  - `/api/setcomfort` – změna komfortních teplot a kalibrace
  - `/api/update` – ruční OTA aktualizace (HTTP OTA)

## ⚙️ Nastavení přes `secrets.h`
```cpp
#define WIFI_SSID "..."
#define WIFI_PASSWORD "..."
#define GITHUB_USER "marek9336"
#define GITHUB_REPO "SIKO_teplomer"
#define GITHUB_BASE_URL "https://" GITHUB_USER ".github.io/" GITHUB_REPO "/Pictures/"
#define OTA_URL "https://marek9336.github.io/SIKO_teplomer/firmware/latest.bin"
```

## 🛠️ Webová administrace – `/settings`
Zde lze nastavit:
- Kalibraci
- Komfortní rozmezí teplot
- OTA URL
- (volitelně přidat SSID, heslo)

## 🧠 Logika meme obrázků:
- `zima_1/2/3.png` – podle míry chladu
- `horko_1/2/3.png` – podle míry horka
- `ok_1.png` – komfortní zóna
- `error.png` – chyba čidla / odpojený kabel

## 🖥️ PowerShell
```powershell
$response = Invoke-RestMethod -Uri "http://IP_ESP/api/temp"
$status = Invoke-RestMethod -Uri "http://IP_ESP/api/status"
Write-Host "Teplota: $($response.temperature) °C (Kalibrace: $($response.calibration))"
Write-Host "Verze: $($status.version)"
```

## 📡 OTA aktualizace
- Firmware si ESP32 stáhne z `OTA_URL`, pokud je novější verze.
- Lze spustit ručně přes `/api/update`.


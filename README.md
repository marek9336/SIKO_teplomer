
# 🌡️ IT Teploměr ESP32

Tento projekt je chytrý teploměr běžící na ESP32 s webovým rozhraním, REST API a podporou OTA aktualizací.

## 📦 Funkce
- Měření teploty přes DS18B20
- Webový dashboard (`/`) s hodinami, grafem, meme obrázkem
- API pro získání a nastavení hodnot (`/api/config`, `/api/temp`, `/api/history`, atd.)
- OTA aktualizace z URL nebo souboru (`/api/update`, `/update`)
- Webová správa na `/settings`

## 🔧 API přehled
- `GET /api/config` – aktuální konfigurace
- `POST /api/config` – změna kalibrace, comfort min/max, interval, délka historie, atd.
- `POST /api/update` – stáhne firmware z URL
- `POST /api/clearhistory` – smaže historii
- `GET /api/temp`, `/api/meme`, `/api/status`, `/api/history`

## 🧪 OTA Update
- `Sketch > Export compiled binary` v Arduino IDE
- Nahraj na GitHub nebo jiný hosting
- Zadej URL do `/settings` nebo použij PowerShell

## 📂 PowerShell
Soubor `get-full-info.ps1` umožní získat a nastavit vše přes API pohodlně z terminálu.

Více info: [https://github.com/marek9336/SIKO_teplomer](https://github.com/marek9336/SIKO_teplomer)

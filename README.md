
# 🌡️ IT Teploměr ESP32

Tento projekt je chytrý teploměr běžící na ESP32 s webovým rozhraním, REST API a podporou OTA aktualizací.

## 📦 Funkce
- Měření teploty přes DS18B20
- Webový dashboard (`/`) s hodinami, grafem, meme obrázkem
- API pro získání a nastavení hodnot (`/api/config`, `/api/temp`, `/api/history`, atd.)
- OTA aktualizace ze souboru (`/update`)
- Webová správa na `/settings`
- Basic Auth ochrana pro nastavení a write endpointy

## 🔧 API přehled
- `GET /api/config` – aktuální konfigurace (chráněno Basic Auth)
- `POST /api/config` – změna konfigurace (chráněno Basic Auth)
- `POST /api/clearhistory` – smaže historii (chráněno Basic Auth)
- `POST /update` – OTA upload `.bin` (chráněno Basic Auth)
- `GET /api/temp`, `/api/meme`, `/api/status`, `/api/history`

## 🔐 Přihlášení do nastavení
- Výchozí Basic Auth:
  - uživatel: `admin`
  - heslo: `change-me`
- Přihlašovací údaje lze změnit:
  - v `/settings` (pole `Admin uživatel` a `Admin heslo`)
  - nebo v `secrets.h` přes `WEB_ADMIN_USER` a `WEB_ADMIN_PASS`

## 🧪 OTA Update
- `Sketch > Export compiled binary` v Arduino IDE
- Nahraj `.bin` přes `/settings` (sekce OTA)

## 📂 PowerShell
Soubor `get-full-info.ps1` umožní získat a nastavit vše přes API pohodlně z terminálu.

Více info: [https://github.com/marek9336/SIKO_teplomer](https://github.com/marek9336/SIKO_teplomer)

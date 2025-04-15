
# Zadej IP ESP32
$ip = "http://192.168.1.123"  # <- Změň podle potřeby

# Načti základní informace
$config = Invoke-RestMethod -Uri "$ip/api/config" -Method Get
Write-Host "`n== Aktuální konfigurace ESP32 ==" -ForegroundColor Cyan
$config | Format-List

# Načti aktuální teplotu a obrázek
$temp = Invoke-RestMethod -Uri "$ip/api/temp" -Method Get
$meme = Invoke-RestMethod -Uri "$ip/api/meme" -Method Get

Write-Host "`n== Aktuální teplota ==" -ForegroundColor Green
$temp

Write-Host "`n== Použitý obrázek ==" -ForegroundColor Green
$meme

# Příklad POST změny nastavení
<# 
$new = @{
    comfortMin = 21.0
    comfortMax = 25.0
    calibration = 0.1
    lastValidTemperature = 22.5
    historyInterval = 15
    historyLength = 48
    ota_url = "https://marek9336.github.io/SIKO_teplomer/firmware/new.bin"
} | ConvertTo-Json -Depth 3

Invoke-RestMethod -Uri "$ip/api/config" -Method Post -Body $new -ContentType "application/json"
#>

# Volitelně vymazat historii
<# Invoke-RestMethod -Uri "$ip/api/clearhistory" -Method POST #>

# Spustit OTA aktualizaci z URL
<# Invoke-RestMethod -Uri "$ip/api/update" -Method POST #>


$response = Invoke-RestMethod -Uri "http://IP_ADRESA/api/temp"
$status = Invoke-RestMethod -Uri "http://IP_ADRESA/api/status"
Write-Host "Teplota: $($response.temperature) °C (Kalibrace: $($response.calibration))"
Write-Host "Senzor: $($response.sensor)"

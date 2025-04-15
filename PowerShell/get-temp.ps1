
$response = Invoke-RestMethod -Uri "http://IP_ESP/api/temp"
$status = Invoke-RestMethod -Uri "http://IP_ESP/api/status"
Write-Host "Teplota: $($response.temperature) °C (Kalibrace: $($response.calibration))"
Write-Host "Verze: $($status.version)"

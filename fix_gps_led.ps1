$portName = "COM7"
$baud = 9600

function Send-Bytes([byte[]]$data) {
    $port = New-Object System.IO.Ports.SerialPort $portName, $baud, None, 8, One
    $port.Open()
    $port.Write($data, 0, $data.Length)
    Start-Sleep -Milliseconds 300
    $port.Close()
    Write-Host "  OK - $($data.Length) bytes sent"
}

# 1) Factory reset: clear all settings, reload ROM defaults on BBR+Flash
$reset = [byte[]]@(
    0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00,
    0xFF, 0xFF, 0x00, 0x00,  # clearMask
    0x00, 0x00, 0x00, 0x00,  # saveMask
    0xFF, 0xFF, 0x00, 0x00,  # loadMask
    0x17,                     # deviceMask: BBR+Flash+EEPROM+SPI
    0x2F, 0xAE                # CK
)

# 2) Save current config to BBR + Flash
$save = [byte[]]@(
    0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03,
    0x1D, 0xAB
)

Write-Host "Close u-center/any program using COM7, then press Enter..."
$null = Read-Host

Write-Host "Step 1: Factory reset..."
Send-Bytes $reset
Start-Sleep -Seconds 2

Write-Host "Step 2: Save defaults to BBR + Flash..."
Send-Bytes $save

Write-Host "`nDone. Power-cycle the module (disconnect/reconnect USB)."
Write-Host "LED should be ON when powered, blink when satellites lock."

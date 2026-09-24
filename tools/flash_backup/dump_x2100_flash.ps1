[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,

    [ValidateRange(1200, 3000000)]
    [int]$BaudRate = 460800,

    [ValidateSet('Config', 'Full')]
    [string]$Region = 'Config',

    [ValidateRange(16, 4096)]
    [int]$ChunkSize = 256,

    [ValidateRange(1, 5)]
    [int]$Retries = 3,

    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
    $OutputDirectory = Join-Path $scriptDirectory 'output'
}

if ($Region -eq 'Config') {
    $startAddress = 0x1DB000
    $totalLength = 0x25000
    $regionLabel = 'config_0x1DB000_0x25000'
} else {
    $startAddress = 0x000000
    $totalLength = 0x200000
    $regionLabel = 'full_flash_0x000000_0x200000'
}

if (($ChunkSize % 16) -ne 0) {
    throw 'ChunkSize must be a multiple of 16 bytes.'
}

# Windows PowerShell 5.1 exposes SerialPort from System.dll, while PowerShell 7
# exposes it from System.IO.Ports.dll. Referencing the type lets each runtime
# resolve its own assembly and avoids Add-Type failures on Windows PowerShell.
try {
    $null = [System.IO.Ports.SerialPort]
} catch {
    throw 'System.IO.Ports.SerialPort is unavailable in this PowerShell runtime.'
}

function Read-AvailableText {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Ports.SerialPort]$SerialPort
    )

    $builder = [System.Text.StringBuilder]::new()
    while ($SerialPort.BytesToRead -gt 0) {
        [void]$builder.Append($SerialPort.ReadExisting())
        Start-Sleep -Milliseconds 5
    }
    return $builder.ToString()
}

function Read-NorChunk {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Ports.SerialPort]$SerialPort,

        [Parameter(Mandatory = $true)]
        [int]$Address,

        [Parameter(Mandatory = $true)]
        [int]$Length,

        [Parameter(Mandatory = $true)]
        [int]$AttemptCount
    )

    for ($attempt = 1; $attempt -le $AttemptCount; $attempt++) {
        # Ctrl+C clears any terminal-identification text left in the shell line buffer.
        $SerialPort.DiscardInBuffer()
        $SerialPort.Write([byte[]]@(3), 0, 1)
        Start-Sleep -Milliseconds 80
        [void](Read-AvailableText -SerialPort $SerialPort)

        $command = 'nor_read 0x{0:X} 0x{1:X}' -f $Address, $Length
        $SerialPort.Write($command + "`r")

        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        $response = [System.Text.StringBuilder]::new()

        while ([DateTime]::UtcNow -lt $deadline) {
            if ($SerialPort.BytesToRead -gt 0) {
                [void]$response.Append($SerialPort.ReadExisting())
                $text = $response.ToString()
                $markerIndex = $text.IndexOf('read_buf:', [StringComparison]::OrdinalIgnoreCase)

                if ($markerIndex -ge 0) {
                    $payload = $text.Substring($markerIndex + 9)
                    $matches = [regex]::Matches(
                        $payload,
                        '(?i)(?<![0-9a-f])0x([0-9a-f]{2})(?![0-9a-f])'
                    )

                    if ($matches.Count -ge $Length) {
                        $bytes = [byte[]]::new($Length)
                        for ($i = 0; $i -lt $Length; $i++) {
                            $bytes[$i] = [Convert]::ToByte($matches[$i].Groups[1].Value, 16)
                        }
                        return ,$bytes
                    }
                }
            }
            Start-Sleep -Milliseconds 10
        }

        $timeoutText = $response.ToString()
        $timeoutMarker = $timeoutText.IndexOf('read_buf:', [StringComparison]::OrdinalIgnoreCase)
        $timeoutTokenCount = 0
        if ($timeoutMarker -ge 0) {
            $timeoutPayload = $timeoutText.Substring($timeoutMarker + 9)
            $timeoutTokenCount = [regex]::Matches(
                $timeoutPayload,
                '(?i)(?<![0-9a-f])0x([0-9a-f]{2})(?![0-9a-f])'
            ).Count
        }
        Write-Warning ('Read timeout at 0x{0:X6}, attempt {1}/{2}; receivedChars={3}, marker={4}, byteTokens={5}/{6}' -f `
            $Address, $attempt, $AttemptCount, $timeoutText.Length, ($timeoutMarker -ge 0), $timeoutTokenCount, $Length)
        if ($timeoutText.Length -gt 0) {
            $escapedTimeoutText = $timeoutText.Replace("`r", '<CR>').Replace("`n", '<LF>')
            if ($escapedTimeoutText.Length -gt 500) {
                $escapedTimeoutText = $escapedTimeoutText.Substring(0, 500) + '...'
            }
            Write-Warning ("Device response: $escapedTimeoutText")
        }
    }

    throw ('Unable to read flash at 0x{0:X6}, length 0x{1:X}' -f $Address, $Length)
}

function Read-NorRegion {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Ports.SerialPort]$SerialPort,

        [Parameter(Mandatory = $true)]
        [int]$StartAddress,

        [Parameter(Mandatory = $true)]
        [int]$Length,

        [Parameter(Mandatory = $true)]
        [int]$BlockSize,

        [Parameter(Mandatory = $true)]
        [int]$AttemptCount,

        [Parameter(Mandatory = $true)]
        [string]$PassName
    )

    $result = [byte[]]::new($Length)
    $completed = 0

    while ($completed -lt $Length) {
        $currentLength = [Math]::Min($BlockSize, $Length - $completed)
        $currentAddress = $StartAddress + $completed
        $chunk = Read-NorChunk -SerialPort $SerialPort -Address $currentAddress `
            -Length $currentLength -AttemptCount $AttemptCount
        [Array]::Copy($chunk, 0, $result, $completed, $currentLength)
        $completed += $currentLength

        $percent = [Math]::Floor(($completed * 100.0) / $Length)
        Write-Progress -Activity "Reading $PassName" `
            -Status ('0x{0:X6}/0x{1:X6}' -f $completed, $Length) `
            -PercentComplete $percent
    }

    Write-Progress -Activity "Reading $PassName" -Completed
    return ,$result
}

$serial = [System.IO.Ports.SerialPort]::new(
    $Port,
    $BaudRate,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serial.Handshake = [System.IO.Ports.Handshake]::None
$serial.DtrEnable = $true
$serial.RtsEnable = $true
$serial.ReadTimeout = 250
$serial.WriteTimeout = 2000
$serial.Encoding = [System.Text.Encoding]::ASCII

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$pass1Path = Join-Path $OutputDirectory "X2100_${regionLabel}_${timestamp}_read1.bin"
$pass2Path = Join-Path $OutputDirectory "X2100_${regionLabel}_${timestamp}_read2.bin"
$verifiedPath = Join-Path $OutputDirectory "X2100_${regionLabel}_${timestamp}_verified.bin"
$manifestPath = Join-Path $OutputDirectory "X2100_${regionLabel}_${timestamp}_manifest.txt"

try {
    $serial.Open()
    Write-Host "Opened $Port at $BaudRate baud. Do not use MobaXterm on this port while dumping."
    Start-Sleep -Seconds 2

    Write-Host 'Probing the Shell with a 32-byte read...'
    $probe = Read-NorChunk -SerialPort $serial -Address 0x1DB000 -Length 0x20 -AttemptCount $Retries
    $expectedProbeMagic = [byte[]](0x56, 0x44, 0x41, 0x52)
    for ($probeIndex = 0; $probeIndex -lt $expectedProbeMagic.Length; $probeIndex++) {
        if ($probe[$probeIndex] -ne $expectedProbeMagic[$probeIndex]) {
            throw ('Shell probe returned an unexpected config magic: {0}' -f `
                (($probe[0..3] | ForEach-Object { '{0:X2}' -f $_ }) -join ' '))
        }
    }
    Write-Host 'Shell probe passed (config magic 56 44 41 52).'

    $pass1 = Read-NorRegion -SerialPort $serial -StartAddress $startAddress `
        -Length $totalLength -BlockSize $ChunkSize -AttemptCount $Retries -PassName 'pass 1 of 2'
    [IO.File]::WriteAllBytes($pass1Path, $pass1)

    $pass2 = Read-NorRegion -SerialPort $serial -StartAddress $startAddress `
        -Length $totalLength -BlockSize $ChunkSize -AttemptCount $Retries -PassName 'pass 2 of 2'
    [IO.File]::WriteAllBytes($pass2Path, $pass2)
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
}

$hash1 = (Get-FileHash -LiteralPath $pass1Path -Algorithm SHA256).Hash
$hash2 = (Get-FileHash -LiteralPath $pass2Path -Algorithm SHA256).Hash

if ($hash1 -ne $hash2) {
    throw "The two reads differ. Files were kept for diagnosis: $pass1Path and $pass2Path"
}

if ($Region -eq 'Config') {
    $expectedMagic = [byte[]](0x56, 0x44, 0x41, 0x52)
    for ($i = 0; $i -lt $expectedMagic.Length; $i++) {
        if ($pass1[$i] -ne $expectedMagic[$i]) {
            throw ('Config magic mismatch at byte {0}: got 0x{1:X2}, expected 0x{2:X2}' -f `
                $i, $pass1[$i], $expectedMagic[$i])
        }
    }
}

[IO.File]::WriteAllBytes($verifiedPath, $pass1)
$manifest = @(
    "Created: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')"
    "Port: $Port"
    "BaudRate: $BaudRate"
    "Region: $Region"
    ('StartAddress: 0x{0:X6}' -f $startAddress)
    ('Length: 0x{0:X6} ({1} bytes)' -f $totalLength, $totalLength)
    "ChunkSize: $ChunkSize"
    "Read1: $pass1Path"
    "Read2: $pass2Path"
    "Verified: $verifiedPath"
    "SHA256: $hash1"
    "DoubleReadMatch: True"
)
$manifest | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host ''
Write-Host 'Backup completed and verified.' -ForegroundColor Green
Write-Host "Verified file: $verifiedPath"
Write-Host "SHA256: $hash1"
Write-Host "Manifest: $manifestPath"

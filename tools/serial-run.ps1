param(
  [string]$EnvName = "sd_http_post_speed_test",
  [string]$Port = "COM15",
  [int]$Baud = 115200,
  [int]$TimeoutSec = 240,
  [string]$LogDir = "logs",
  [string]$ResetCommand = "reset",
  [switch]$NoFlash
)

$ErrorActionPreference = "Stop"

function Assert-LastExitCode([string]$StepName) {
  if ($LASTEXITCODE -ne 0) {
    throw "$StepName failed with exit code $LASTEXITCODE"
  }
}

function Normalize-StatKey([string]$RawKey) {
  $k = $RawKey.Trim().ToLowerInvariant()
  $k = $k -replace '[^a-z0-9]+', '_'
  $k = $k.Trim('_')
  return $k
}

function Parse-RunSummary([string[]]$Lines) {
  $rows = New-Object System.Collections.Generic.List[object]
  $current = $null

  foreach ($raw in $Lines) {
    $line = $raw.Trim()
    if ([string]::IsNullOrWhiteSpace($line)) {
      continue
    }
    if ($line.StartsWith("=====")) {
      continue
    }

    if ($line.StartsWith("Run:")) {
      if ($current -ne $null) {
        $rows.Add([pscustomobject]$current)
      }
      $current = [ordered]@{
        run = $line.Substring(4).Trim()
      }
      continue
    }

    if ($current -eq $null) {
      continue
    }

    $parts = $line.Split(':', 2)
    if ($parts.Length -eq 2) {
      $key = Normalize-StatKey $parts[0]
      $value = $parts[1].Trim()
      if ($key.Length -gt 0) {
        $current[$key] = $value
      }
    }
  }

  if ($current -ne $null) {
    $rows.Add([pscustomobject]$current)
  }

  return $rows
}

function Build-SummaryText([object[]]$Rows) {
  if ($Rows.Count -eq 0) {
    return "No runs found."
  }

  $compactCols = @(
    "run",
    "result",
    "http_status",
    "elapsed_ms",
    "upload_mb_s",
    "max_blocking_us_between_yields"
  )
  $presentCompact = @()
  foreach ($c in $compactCols) {
    if ($Rows[0].PSObject.Properties.Name -contains $c) {
      $presentCompact += $c
    }
  }

  $tableText = ""
  if ($presentCompact.Count -gt 0) {
    $tableText = ($Rows | Select-Object $presentCompact | Format-Table -AutoSize | Out-String)
  }

  $details = New-Object System.Text.StringBuilder
  [void]$details.AppendLine("Detailed per-run stats")
  [void]$details.AppendLine("----------------------")
  foreach ($row in $Rows) {
    [void]$details.AppendLine("Run: $($row.run)")
    foreach ($p in $row.PSObject.Properties) {
      if ($p.Name -eq "run") {
        continue
      }
      [void]$details.AppendLine("  $($p.Name): $($p.Value)")
    }
    [void]$details.AppendLine("")
  }

  return ($tableText + "`r`n" + $details.ToString())
}

$pio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"
if (-not (Test-Path $pio)) {
  throw "PlatformIO executable not found: $pio"
}

if (-not $NoFlash) {
  Write-Host "[serial-run] Flashing env '$EnvName'..."
  & $pio run -t upload -e $EnvName
  Assert-LastExitCode "Flash"
}

New-Item -ItemType Directory -Force $LogDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logPath = Join-Path $LogDir "sd_http_post_speed_$stamp.log"
$summaryPath = Join-Path $LogDir "sd_http_post_speed_$stamp.summary.txt"

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 300
$sp.WriteTimeout = 1000
$sp.NewLine = "`n"

Write-Host "[serial-run] Opening serial $Port @ $Baud..."
$sp.Open()
Start-Sleep -Milliseconds 400
$sp.DiscardInBuffer()
Write-Host "[serial-run] Sending '$ResetCommand'..."
$sp.WriteLine($ResetCommand)

$deadline = (Get-Date).AddSeconds($TimeoutSec)
$lines = New-Object System.Collections.Generic.List[string]

Write-Host "[serial-run] Capturing output..."
while ((Get-Date) -lt $deadline) {
  try {
    $line = $sp.ReadLine()
    if ($null -ne $line) {
      $lines.Add($line)
      if ($line -match "===== END SUITE: minimize-blocking =====") {
        break
      }
    }
  } catch [System.TimeoutException] {
  }
}

$sp.Close()
$sp.Dispose()

$lines | Set-Content -Path $logPath -Encoding UTF8
Write-Host "[serial-run] Log saved: $logPath"

$summaryRows = Parse-RunSummary -Lines $lines
if ($summaryRows.Count -eq 0) {
  Write-Warning "No run summary rows parsed from log."
  exit 0
}

$summaryText = Build-SummaryText -Rows $summaryRows

$summaryText | Set-Content -Path $summaryPath -Encoding UTF8
Write-Host "[serial-run] Summary saved: $summaryPath"
Write-Host ""
Write-Host $summaryText

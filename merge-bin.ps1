#!/usr/bin/env pwsh

$ErrorActionPreference = "Stop"

# Binary file paths and addresses
$BOOTLOADER_BIN      = "build/bootloader/bootloader.bin"
$BOOTLOADER_BIN_ADDR = "0x0"

$PARTITION_TABLE      = "build/partition_table/partition-table.bin"
$PARTITION_TABLE_ADDR = "0x8000"

$CONFIG_BIN      = "config.bin"
$CONFIG_BIN_ADDR = "0x9000"

$MINER_BIN      = "build/esp-miner.bin"
$MINER_BIN_ADDR = "0x10000"

$WWW_BIN      = "build/www.bin"
$WWW_BIN_ADDR = "0x410000"

$OTA_BIN      = "build/ota_data_initial.bin"
$OTA_BIN_ADDR = "0xf10000"

$BINS_DEFAULT = @(
    $BOOTLOADER_BIN,
    $PARTITION_TABLE,
    $MINER_BIN,
    $WWW_BIN,
    $OTA_BIN
)

$BINS_AND_ADDRS_DEFAULT = @(
    $BOOTLOADER_BIN_ADDR, $BOOTLOADER_BIN,
    $PARTITION_TABLE_ADDR, $PARTITION_TABLE,
    $MINER_BIN_ADDR, $MINER_BIN,
    $WWW_BIN_ADDR, $WWW_BIN,
    $OTA_BIN_ADDR, $OTA_BIN
)

$BINS_WITH_CONFIG = @(
    $BOOTLOADER_BIN,
    $PARTITION_TABLE,
    $CONFIG_BIN,
    $MINER_BIN,
    $WWW_BIN,
    $OTA_BIN
)

$BINS_AND_ADDRS_WITH_CONFIG = @(
    $BOOTLOADER_BIN_ADDR, $BOOTLOADER_BIN,
    $PARTITION_TABLE_ADDR, $PARTITION_TABLE,
    $CONFIG_BIN_ADDR, $CONFIG_BIN,
    $MINER_BIN_ADDR, $MINER_BIN,
    $WWW_BIN_ADDR, $WWW_BIN,
    $OTA_BIN_ADDR, $OTA_BIN
)

$BINS_UPDATE = @(
    $MINER_BIN,
    $WWW_BIN,
    $OTA_BIN
)

$BINS_AND_ADDRS_UPDATE = @(
    $MINER_BIN_ADDR, $MINER_BIN,
    $WWW_BIN_ADDR, $WWW_BIN,
    $OTA_BIN_ADDR, $OTA_BIN
)

function Show-Help {
    $scriptName = Split-Path -Leaf $PSCommandPath
    if ([string]::IsNullOrWhiteSpace($scriptName)) {
        $scriptName = "merge-bin.ps1"
    }

    Write-Host "Creates a combined binary using esptool's merge_bin command"
    Write-Host "Usage: .\$scriptName [OPTION] output_file"
    Write-Host "    output_file: The combined binary"
    Write-Host "    Options:"
    Write-Host "        -c: Include $CONFIG_BIN in addition to default binaries into output_file"
    Write-Host "        -u: Only include the following binaries into output_file:"
    Write-Host "            $($BINS_UPDATE -join ' ')"
    Write-Host "        -h: Show this help"
    Write-Host ""
    Write-Host "    If no options are specified, by default the following binaries are included:"
    Write-Host "        $($BINS_DEFAULT -join ' ')"
    Write-Host ""
}

function Write-ErrorHeader {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Message
    )

    Write-Host "ERROR: $Message"
}

function Get-EsptoolInvocation {
    # Prefer direct esptool launchers if they are available.
    foreach ($commandName in @("esptool.py", "esptool")) {
        if (Get-Command $commandName -ErrorAction SilentlyContinue) {
            return @{
                File = $commandName
                Args = @()
            }
        }
    }

    # Windows-friendly fallback: python -m esptool
    foreach ($pythonName in @("py", "python", "python3")) {
        if (Get-Command $pythonName -ErrorAction SilentlyContinue) {
            & $pythonName -m esptool version *> $null

            if ($LASTEXITCODE -eq 0) {
                return @{
                    File = $pythonName
                    Args = @("-m", "esptool")
                }
            }
        }
    }

    return $null
}

# ---- argument parsing ----

$updateOnly = $false
$withConfig = $false
$showHelp = $false
$remainingArgs = @()

for ($i = 0; $i -lt $args.Count; $i++) {
    $arg = [string]$args[$i]

    if ($arg -eq "--") {
        if ($i + 1 -lt $args.Count) {
            $remainingArgs += $args[($i + 1)..($args.Count - 1)]
        }
        break
    }

    if ($arg.StartsWith("-") -and $arg.Length -gt 1) {
        $flags = $arg.Substring(1).ToCharArray()

        foreach ($flag in $flags) {
            switch ($flag) {
                "h" {
                    $showHelp = $true
                }
                "u" {
                    $updateOnly = $true
                }
                "c" {
                    $withConfig = $true
                }
                default {
                    Show-Help
                    exit 1
                }
            }
        }

        continue
    }

    $remainingArgs += $arg

    if ($i + 1 -lt $args.Count) {
        $remainingArgs += $args[($i + 1)..($args.Count - 1)]
    }

    break
}

if ($showHelp) {
    Show-Help
    exit 0
}

$outputFile = $null
if ($remainingArgs.Count -gt 0) {
    $outputFile = [string]$remainingArgs[0]
}

if ([string]::IsNullOrWhiteSpace($outputFile)) {
    Write-ErrorHeader "output_file missing"
    Write-Host ""
    Show-Help
    exit 2
}

if ($updateOnly -and $withConfig) {
    Write-ErrorHeader "Include only one of '-c' and '-u'"
    exit 3
}

$esptool = Get-EsptoolInvocation
if ($null -eq $esptool) {
    Write-Host "esptool is not installed or not accessible. Please install it first."
    Write-Host "pip install esptool"
    exit 1
}

$selectedBins = @()
$selectedBinsAndAddrs = @()

$esptoolLeadingArgs = @(
    "--chip", "esp32s3",
    "merge_bin",
    "--flash_mode", "dio",
    "--flash_size", "16MB",
    "--flash_freq", "80m"
)

if ($updateOnly) {
    $selectedBins = $BINS_UPDATE
    $selectedBinsAndAddrs = $BINS_AND_ADDRS_UPDATE
    $esptoolLeadingArgs += @("--format", "hex")
}
elseif ($withConfig) {
    $selectedBins = $BINS_WITH_CONFIG
    $selectedBinsAndAddrs = $BINS_AND_ADDRS_WITH_CONFIG
}
else {
    $selectedBins = $BINS_DEFAULT
    $selectedBinsAndAddrs = $BINS_AND_ADDRS_DEFAULT
}

foreach ($file in $selectedBins) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        Write-ErrorHeader "Required file $file does not exist. Make sure to build first."
        Write-Host "Exiting"
        exit 4
    }
}

$esptoolArgs = @()
$esptoolArgs += $esptool.Args
$esptoolArgs += $esptoolLeadingArgs
$esptoolArgs += $selectedBinsAndAddrs
$esptoolArgs += @("-o", $outputFile)

& $esptool.File @esptoolArgs

if ($LASTEXITCODE -eq 0) {
    Write-Host "Successfully created $outputFile"
}
else {
    Write-ErrorHeader "Failed to create $outputFile"
    exit 5
}

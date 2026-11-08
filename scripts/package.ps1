# Makes the portable Windows zip after a build.
param(
    [string]$BuildDir = "build/Release",
    [string]$OutDir = "dist",
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if ([string]::IsNullOrWhiteSpace($Version)) {
    $match = Select-String -Path CMakeLists.txt -Pattern 'project\(RadiantFighter VERSION ([0-9.]+)'
    if (-not $match) {
        throw "Could not determine project version from CMakeLists.txt"
    }
    $Version = $match.Matches[0].Groups[1].Value
}

$exe = Join-Path $BuildDir "radiantfighter.exe"

if (-not (Test-Path $exe)) {
    throw "$exe not found: build the Release configuration first"
}

$stage = Join-Path $OutDir "radiantfighter"

if (Test-Path $stage) {
    Remove-Item -Recurse -Force $stage
}

New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item $exe, README.md, LICENSE, CHANGELOG.md -Destination $stage

$zip = Join-Path $OutDir "radiantfighter-$Version-windows-x64.zip"

if (Test-Path $zip) {
    Remove-Item -Force $zip
}

Compress-Archive -Path $stage -DestinationPath $zip

Write-Host "Created $zip"
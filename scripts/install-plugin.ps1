[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot 'build'
$source = Get-ChildItem -LiteralPath $buildDir -Directory -Filter 'ConvbasedVST3.vst3' -Recurse |
    Where-Object { $_.FullName -match [regex]::Escape($Configuration) } |
    Select-Object -First 1
if (-not $source) {
    throw "No $Configuration ConvbasedVST3.vst3 package was found. Run scripts/build.ps1 first."
}

$destinationRoot = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
New-Item -ItemType Directory -Path $destinationRoot -Force | Out-Null
Copy-Item -LiteralPath $source.FullName -Destination $destinationRoot -Recurse -Force
Write-Host "Installed plug-in to: $(Join-Path $destinationRoot $source.Name)"

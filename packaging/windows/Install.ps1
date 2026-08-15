[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'Plugin\ConvbasedVST3.vst3'
if (-not (Test-Path -LiteralPath $source)) {
    throw "Packaged VST3 bundle was not found: $source"
}

$destinationRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3')
)
$destination = [System.IO.Path]::GetFullPath(
    (Join-Path $destinationRoot 'ConvbasedVST3.vst3')
)
$prefix = $destinationRoot.TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar
) + [System.IO.Path]::DirectorySeparatorChar
if (-not $destination.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Installation path escapes the per-user VST3 directory: $destination"
}

New-Item -ItemType Directory -Path $destinationRoot -Force | Out-Null
if (Test-Path -LiteralPath $destination) {
    Remove-Item -LiteralPath $destination -Recurse -Force
}
Copy-Item -LiteralPath $source -Destination $destinationRoot -Recurse
$legacyCredential = [System.IO.Path]::GetFullPath(
    (Join-Path $env:LOCALAPPDATA 'Convbased\VST3\api-key.bin')
)
if (Test-Path -LiteralPath $legacyCredential) {
    Remove-Item -LiteralPath $legacyCredential -Force
}
Write-Host "Installed Convbased Bridge to: $destination"
Write-Host 'Rescan VST3 plug-ins in the DAW before testing.'

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot 'build'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer (vswhere.exe) was not found. Install Visual Studio Build Tools with Desktop development with C++.'
}

$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) {
    throw 'No Visual Studio C++ toolchain was found.'
}

$installationVersion = & $vswhere -latest -products * -property installationVersion
$major = [int]($installationVersion -split '\.')[0]
$generator = switch ($major) {
    18 { 'Visual Studio 18 2026' }
    17 { 'Visual Studio 17 2022' }
    16 { 'Visual Studio 16 2019' }
    default { throw "Unsupported Visual Studio major version: $major" }
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1
if (-not $cmake) {
    $cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not (Test-Path -LiteralPath $cmake)) {
    throw 'CMake was not found in PATH or the Visual Studio installation.'
}

& $cmake -S $repoRoot -B $buildDir -G $generator -A x64 '-DBUILD_TESTING=ON' '-DSMTG_CREATE_PLUGIN_LINK=0'
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}

& $cmake --build $buildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE"
}

$plugin = Get-ChildItem -LiteralPath $buildDir -Directory -Filter 'ConvbasedVST3.vst3' -Recurse |
    Where-Object { $_.FullName -match [regex]::Escape($Configuration) } |
    Select-Object -First 1
if ($plugin) {
    Write-Host "Built plug-in: $($plugin.FullName)"
} else {
    Write-Warning 'The build succeeded, but the packaged .vst3 directory was not located automatically.'
}

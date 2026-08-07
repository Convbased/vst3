[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Debug',
    [string]$FlStudioPath,
    [string]$Python,
    [ValidateRange(1, 65535)]
    [int]$Port = 43127,
    [ValidateSet('CurrentUser', 'AllUsers')]
    [string]$InstallScope = 'AllUsers',
    [switch]$SkipBuild,
    [switch]$SkipInstall,
    [switch]$SkipLaunch,
    [switch]$SkipBridge
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

function Resolve-FlStudioPath {
    param([string]$RequestedPath)

    if ($RequestedPath) {
        $resolved = [System.IO.Path]::GetFullPath($RequestedPath)
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "FL Studio was not found at: $resolved"
        }
        return $resolved
    }

    $imageLineRoot = Join-Path $env:ProgramFiles 'Image-Line'
    if (Test-Path -LiteralPath $imageLineRoot) {
        $detected = Get-ChildItem -LiteralPath $imageLineRoot -Directory -Filter 'FL Studio *' |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName 'FL64.exe' } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
            Select-Object -First 1
        if ($detected) {
            return $detected
        }
    }

    throw 'FL Studio was not found. Pass -FlStudioPath with the full path to FL64.exe.'
}

$resolvedFlStudioPath = Resolve-FlStudioPath -RequestedPath $FlStudioPath
$flStudioProcessName = [System.IO.Path]::GetFileNameWithoutExtension($resolvedFlStudioPath)
$runningFlStudio = Get-Process -Name $flStudioProcessName -ErrorAction SilentlyContinue

if (-not $SkipInstall -and $runningFlStudio) {
    throw 'FL Studio is running. Close it before installing a rebuilt plug-in, then run this script again.'
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
}

if (-not $SkipInstall) {
    & (Join-Path $PSScriptRoot 'install-plugin.ps1') -Configuration $Configuration -Scope $InstallScope
}

$venvPython = Join-Path $repoRoot '.venv\Scripts\python.exe'
if (-not $SkipBridge -and -not (Test-Path -LiteralPath $venvPython -PathType Leaf)) {
    if (-not $Python) {
        throw 'The bridge-service environment is missing. Pass -Python with a Python 3.10+ executable.'
    }
    & (Join-Path $PSScriptRoot 'setup-bridge.ps1') -Python $Python
}

if (-not $SkipLaunch) {
    $runningFlStudio = Get-Process -Name $flStudioProcessName -ErrorAction SilentlyContinue
    if (-not $runningFlStudio) {
        Start-Process -FilePath $resolvedFlStudioPath | Out-Null
        Write-Host "Started FL Studio: $resolvedFlStudioPath"
    } else {
        Write-Host 'FL Studio is already running.'
    }
}

$solution = Join-Path $repoRoot 'build\ConvbasedVST3.sln'
$installedPlugin = if ($InstallScope -eq 'AllUsers') {
    Join-Path $env:ProgramFiles 'Common Files\VST3\ConvbasedVST3.vst3'
} else {
    Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3\ConvbasedVST3.vst3'
}
Write-Host "Visual Studio solution: $solution"
Write-Host "Installed plug-in: $installedPlugin"

if (-not $SkipBridge) {
    Write-Host 'Starting the bridge service with verbose logs. Press Ctrl+C to stop it.'
    & (Join-Path $PSScriptRoot 'run-bridge.ps1') -Port $Port -VerboseLogs
}

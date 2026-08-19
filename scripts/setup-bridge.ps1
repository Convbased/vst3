[CmdletBinding()]
param(
    [string]$Python,
    [string]$SdkPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $SdkPath) {
    $SdkPath = Join-Path $repoRoot 'third_party\convbased-sdk-python'
}
$SdkPath = [System.IO.Path]::GetFullPath($SdkPath)
if (-not (Test-Path -LiteralPath (Join-Path $SdkPath 'pyproject.toml'))) {
    throw "The vendored Convbased Python SDK was not found at: $SdkPath"
}

if (-not $Python) {
    $Python = Get-Command python -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1
}
if (-not $Python) {
    throw 'Python was not found. Pass -Python with a Python 3.10+ executable.'
}

$venv = Join-Path $repoRoot '.venv'
if (-not (Test-Path -LiteralPath (Join-Path $venv 'Scripts\python.exe'))) {
    & $Python -m venv $venv
    if ($LASTEXITCODE -ne 0) {
        throw "Creating the virtual environment failed with exit code $LASTEXITCODE"
    }
}

$venvPython = Join-Path $venv 'Scripts\python.exe'
& $venvPython -m pip install --upgrade pip
if ($LASTEXITCODE -ne 0) {
    throw "Updating pip failed with exit code $LASTEXITCODE"
}
& $venvPython -m pip install -e $SdkPath -e (Join-Path $repoRoot 'bridge')
if ($LASTEXITCODE -ne 0) {
    throw "Installing bridge-service dependencies failed with exit code $LASTEXITCODE"
}

Write-Host "Bridge-service environment ready: $venv"

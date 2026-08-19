[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [switch]$SkipBuild,
    [string]$Python
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
}

$ctest = Get-Command ctest -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1
if (-not $ctest) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsRoot = & $vswhere -latest -products * -property installationPath
    $ctest = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
}
& $ctest --test-dir (Join-Path $repoRoot 'build') -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "C++ tests failed with exit code $LASTEXITCODE"
}

if (-not $Python) {
    $venvPython = Join-Path $repoRoot '.venv\Scripts\python.exe'
    if (Test-Path -LiteralPath $venvPython) {
        $Python = $venvPython
    } else {
        $Python = Get-Command python -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1
    }
}
if (-not $Python) {
    throw 'Python was not found. Pass -Python with a Python 3.10+ executable.'
}

$oldPythonPath = $env:PYTHONPATH
try {
    $env:PYTHONPATH = Join-Path $repoRoot 'bridge\src'
    & $Python -m unittest discover -s (Join-Path $repoRoot 'bridge\tests') -v
    if ($LASTEXITCODE -ne 0) {
        throw "Python tests failed with exit code $LASTEXITCODE"
    }
} finally {
    $env:PYTHONPATH = $oldPythonPath
}

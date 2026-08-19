[CmdletBinding()]
param(
    [int]$Port = 43127,
    [switch]$VerboseLogs
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$python = Join-Path $repoRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) {
    throw 'The bridge-service environment is missing. Run scripts/setup-bridge.ps1 first.'
}
$arguments = @(
    '-m', 'convbased_vst_bridge',
    '--port', $Port
)
if ($VerboseLogs) {
    $arguments += '--verbose'
}
& $python @arguments

[CmdletBinding()]
param(
    [switch]$VerboseLogs
)

$ErrorActionPreference = 'Stop'

$executable = Join-Path $PSScriptRoot 'Sidecar\ConvbasedVSTBridge\ConvbasedVSTBridge.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Packaged sidecar was not found: $executable"
}

$arguments = @()
if ($VerboseLogs) {
    $arguments += '--verbose'
}
& $executable @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Convbased sidecar exited with code $LASTEXITCODE"
}

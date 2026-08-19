[CmdletBinding()]
param(
    [switch]$VerboseLogs
)

$ErrorActionPreference = 'Stop'

$executable = Join-Path $PSScriptRoot 'BridgeService\ConvbasedVSTBridge\ConvbasedVSTBridge.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Packaged bridge service was not found: $executable"
}

$arguments = @()
if ($VerboseLogs) {
    $arguments += '--verbose'
}
& $executable @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Convbased bridge service exited with code $LASTEXITCODE"
}

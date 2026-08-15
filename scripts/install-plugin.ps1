[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [ValidateSet('CurrentUser', 'AllUsers')]
    [string]$Scope = 'CurrentUser'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot 'build'

if ($Scope -eq 'AllUsers') {
    $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    $isAdministrator = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdministrator) {
        $arguments = @(
            '-NoProfile',
            '-ExecutionPolicy',
            'Bypass',
            '-File',
            ('"{0}"' -f $PSCommandPath),
            '-Configuration',
            $Configuration,
            '-Scope',
            $Scope
        )
        $elevated = Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -Verb RunAs -Wait -PassThru -WindowStyle Hidden
        if ($elevated.ExitCode -ne 0) {
            throw "Elevated plug-in installation failed with exit code $($elevated.ExitCode)"
        }
        return
    }
}

$source = Get-ChildItem -LiteralPath $buildDir -Directory -Filter 'ConvbasedVST3.vst3' -Recurse |
    Where-Object { $_.FullName -match [regex]::Escape($Configuration) } |
    Select-Object -First 1
if (-not $source) {
    throw "No $Configuration ConvbasedVST3.vst3 package was found. Run scripts/build.ps1 first."
}

$destinationRoot = if ($Scope -eq 'AllUsers') {
    Join-Path $env:ProgramFiles 'Common Files\VST3'
} else {
    Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
}
New-Item -ItemType Directory -Path $destinationRoot -Force | Out-Null
Copy-Item -LiteralPath $source.FullName -Destination $destinationRoot -Recurse -Force
$legacyCredential = [System.IO.Path]::GetFullPath(
    (Join-Path $env:LOCALAPPDATA 'Convbased\VST3\api-key.bin')
)
if (Test-Path -LiteralPath $legacyCredential) {
    Remove-Item -LiteralPath $legacyCredential -Force
}
Write-Host "Installed plug-in to: $(Join-Path $destinationRoot $source.Name)"

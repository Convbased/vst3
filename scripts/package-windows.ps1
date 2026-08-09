[CmdletBinding()]
param(
    [string]$Version = '0.1.0-alpha.3',
    [ValidateSet('Release')]
    [string]$Configuration = 'Release',
    [switch]$SkipBuild,
    [switch]$SkipDependencyInstall
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

if ([System.Environment]::OSVersion.Platform -ne [System.PlatformID]::Win32NT) {
    throw 'The Windows package must be built on Windows.'
}
if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'X64') {
    throw 'This script produces the windows-x64 package and must run on an x64 host.'
}
if ($Version -notmatch '^[0-9A-Za-z][0-9A-Za-z.-]+$') {
    throw 'Version contains unsupported package-name characters.'
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'test.ps1') -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Build and tests failed with exit code $LASTEXITCODE"
    }
}

$python = Join-Path $repoRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) {
    throw 'The bridge-service environment is missing. Run scripts/setup-bridge.ps1 first.'
}
if (-not $SkipDependencyInstall) {
    & $python -m pip install 'pyinstaller>=6.11,<7'
    if ($LASTEXITCODE -ne 0) {
        throw "Installing PyInstaller failed with exit code $LASTEXITCODE"
    }
}
& $python -c 'import PyInstaller'
if ($LASTEXITCODE -ne 0) {
    throw 'PyInstaller is unavailable. Install bridge-service release dependencies first.'
}

$buildDir = Join-Path $repoRoot 'build'
$plugin = Get-ChildItem -LiteralPath $buildDir -Directory -Filter 'ConvbasedVST3.vst3' -Recurse |
    Where-Object { $_.FullName -match [regex]::Escape($Configuration) } |
    Select-Object -First 1
if (-not $plugin) {
    throw "No $Configuration ConvbasedVST3.vst3 bundle was found."
}

$outRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'out'))
$packageName = "ConvbasedVST3-$Version-windows-x64"
$stage = [System.IO.Path]::GetFullPath((Join-Path $outRoot $packageName))
$pyInstallerRoot = [System.IO.Path]::GetFullPath((Join-Path $outRoot '.pyinstaller-windows-x64'))
$zipPath = [System.IO.Path]::GetFullPath((Join-Path $outRoot "$packageName.zip"))
$zipHashPath = "$zipPath.sha256"

function Assert-PathInsideOut([string]$Path) {
    $prefix = $outRoot.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar
    if (-not $Path.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Packaging path escapes the output directory: $Path"
    }
}

foreach ($path in @($stage, $pyInstallerRoot, $zipPath, $zipHashPath)) {
    Assert-PathInsideOut $path
}
New-Item -ItemType Directory -Path $outRoot -Force | Out-Null
foreach ($path in @($stage, $pyInstallerRoot)) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force
    }
}
foreach ($path in @($zipPath, $zipHashPath)) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Force
    }
}

$pyInstallerDist = Join-Path $pyInstallerRoot 'dist'
$pyInstallerWork = Join-Path $pyInstallerRoot 'work'
$pyInstallerSpec = Join-Path $pyInstallerRoot 'spec'
New-Item -ItemType Directory -Path $pyInstallerDist, $pyInstallerWork, $pyInstallerSpec -Force |
    Out-Null

$entryPoint = Join-Path $repoRoot 'packaging\windows\bridge_entry.py'
$pyInstallerArgs = @(
    '--noconfirm',
    '--clean',
    '--onedir',
    '--console',
    '--name', 'ConvbasedVSTBridge',
    '--distpath', $pyInstallerDist,
    '--workpath', $pyInstallerWork,
    '--specpath', $pyInstallerSpec,
    '--paths', (Join-Path $repoRoot 'bridge\src'),
    '--collect-all', 'convbased',
    '--collect-all', 'convbased_vst_bridge',
    '--collect-all', 'aiortc',
    '--collect-all', 'av',
    $entryPoint
)
& $python -m PyInstaller @pyInstallerArgs
if ($LASTEXITCODE -ne 0) {
    throw "Building the standalone bridge service failed with exit code $LASTEXITCODE"
}

$frozenBridge = Join-Path $pyInstallerDist 'ConvbasedVSTBridge'
$frozenExe = Join-Path $frozenBridge 'ConvbasedVSTBridge.exe'
if (-not (Test-Path -LiteralPath $frozenExe)) {
    throw "The standalone bridge-service executable was not produced: $frozenExe"
}

$pluginDir = Join-Path $stage 'Plugin'
$bridgeServiceDir = Join-Path $stage 'BridgeService'
New-Item -ItemType Directory -Path $pluginDir, $bridgeServiceDir -Force | Out-Null
Copy-Item -LiteralPath $plugin.FullName -Destination $pluginDir -Recurse
Copy-Item -LiteralPath $frozenBridge -Destination $bridgeServiceDir -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot 'packaging\windows\Install.ps1') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repoRoot 'packaging\windows\Start-Bridge.ps1') -Destination $stage

$commit = (& git -C $repoRoot rev-parse HEAD).Trim()
$readmeTemplate = Get-Content -LiteralPath (
    Join-Path $repoRoot 'packaging\windows\README.template.md'
) -Raw
$readme = $readmeTemplate.Replace('{{VERSION}}', $Version).Replace('{{COMMIT}}', $commit)
[System.IO.File]::WriteAllText(
    (Join-Path $stage 'README.md'),
    $readme,
    [System.Text.UTF8Encoding]::new($false)
)

$pluginBinary = Get-ChildItem -LiteralPath $pluginDir -File -Filter 'ConvbasedVST3.vst3' -Recurse |
    Select-Object -First 1
$checksumTargets = @($pluginBinary, (Get-Item -LiteralPath (Join-Path $bridgeServiceDir 'ConvbasedVSTBridge\ConvbasedVSTBridge.exe')))
$checksumLines = foreach ($target in $checksumTargets) {
    $hash = Get-FileHash -LiteralPath $target.FullName -Algorithm SHA256
    $relative = $target.FullName.Substring($stage.Length).TrimStart('\', '/').Replace('\', '/')
    "$($hash.Hash.ToLowerInvariant())  $relative"
}
[System.IO.File]::WriteAllLines(
    (Join-Path $stage 'SHA256SUMS.txt'),
    $checksumLines,
    [System.Text.UTF8Encoding]::new($false)
)

Compress-Archive -LiteralPath $stage -DestinationPath $zipPath -CompressionLevel Optimal
$zipHash = Get-FileHash -LiteralPath $zipPath -Algorithm SHA256
[System.IO.File]::WriteAllText(
    $zipHashPath,
    "$($zipHash.Hash.ToLowerInvariant())  $([System.IO.Path]::GetFileName($zipPath))`n",
    [System.Text.UTF8Encoding]::new($false)
)

Remove-Item -LiteralPath $stage, $pyInstallerRoot -Recurse -Force
Write-Host "Package: $zipPath"
Write-Host "SHA256: $($zipHash.Hash.ToLowerInvariant())"

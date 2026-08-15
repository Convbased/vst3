[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$ZipPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$zip = Get-Item -LiteralPath $ZipPath
if ($zip.Extension -ne '.zip') {
    throw "Expected a ZIP package: $($zip.FullName)"
}

$outerChecksumPath = "$($zip.FullName).sha256"
if (Test-Path -LiteralPath $outerChecksumPath) {
    $expected = ((Get-Content -LiteralPath $outerChecksumPath -Raw) -split '\s+')[0]
    $actual = (Get-FileHash -LiteralPath $zip.FullName -Algorithm SHA256).Hash
    if ($actual -ne $expected) {
        throw 'ZIP SHA256 does not match its checksum file.'
    }
}

function Get-PeMachine([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw "Not a PE executable: $Path"
    }
    $peOffset = [System.BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -lt 0 -or $peOffset + 6 -gt $bytes.Length) {
        throw "Invalid PE header offset: $Path"
    }
    if (
        $bytes[$peOffset] -ne 0x50 -or
        $bytes[$peOffset + 1] -ne 0x45 -or
        $bytes[$peOffset + 2] -ne 0 -or
        $bytes[$peOffset + 3] -ne 0
    ) {
        throw "Invalid PE signature: $Path"
    }
    return [System.BitConverter]::ToUInt16($bytes, $peOffset + 4)
}

$tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$smokeRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $tempRoot ('convbased-vst-smoke-' + [guid]::NewGuid().ToString('N')))
)
$tempPrefix = $tempRoot.TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar
) + [System.IO.Path]::DirectorySeparatorChar
if (-not $smokeRoot.StartsWith($tempPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Smoke-test path escapes the system temp directory: $smokeRoot"
}

New-Item -ItemType Directory -Path $smokeRoot | Out-Null
try {
    Expand-Archive -LiteralPath $zip.FullName -DestinationPath $smokeRoot
    $packageRoots = @(Get-ChildItem -LiteralPath $smokeRoot -Directory)
    if ($packageRoots.Count -ne 1) {
        throw 'The package must contain exactly one root directory.'
    }
    $packageRoot = $packageRoots[0].FullName

    $packageReadmePath = Join-Path $packageRoot 'README.md'
    if (-not (Test-Path -LiteralPath $packageReadmePath)) {
        throw 'The package README is missing.'
    }
    $packageReadme = Get-Content -LiteralPath $packageReadmePath -Raw
    if ($packageReadme -match '\{\{(?:VERSION|COMMIT)\}\}') {
        throw 'The package README contains unresolved template fields.'
    }
    if (
        $packageReadme -notmatch 'current process session' -or
        $packageReadme -notmatch 'Converted output stays silent' -or
        $packageReadme -notmatch 'never write it to disk'
    ) {
        throw 'The package README does not describe the current credential and audio behavior.'
    }
    if (
        $packageReadme -match 'falls back to dry audio' -or
        $packageReadme -match 'Windows DPAPI'
    ) {
        throw 'The package README still contains obsolete runtime behavior.'
    }

    $internalChecksumPath = Join-Path $packageRoot 'SHA256SUMS.txt'
    foreach ($line in Get-Content -LiteralPath $internalChecksumPath) {
        if ($line -notmatch '^([0-9a-fA-F]{64})  (.+)$') {
            throw "Invalid internal checksum line: $line"
        }
        $expected = $Matches[1]
        $relative = $Matches[2].Replace('/', [System.IO.Path]::DirectorySeparatorChar)
        $target = [System.IO.Path]::GetFullPath((Join-Path $packageRoot $relative))
        $packagePrefix = $packageRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
        if (-not $target.StartsWith($packagePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Checksum target escapes the package: $relative"
        }
        $actual = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
        if ($actual -ne $expected) {
            throw "Internal checksum mismatch: $relative"
        }
    }

    $bridgeService = Join-Path $packageRoot 'BridgeService\ConvbasedVSTBridge\ConvbasedVSTBridge.exe'
    $pluginBundle = Join-Path $packageRoot 'Plugin\ConvbasedVST3.vst3'
    $pluginBinary = Get-ChildItem -LiteralPath $pluginBundle -File -Filter 'ConvbasedVST3.vst3' -Recurse |
        Select-Object -First 1
    if (-not $pluginBinary) {
        throw 'The packaged VST3 module binary is missing.'
    }

    $amd64Machine = 0x8664
    foreach ($binary in @($bridgeService, $pluginBinary.FullName)) {
        $machine = Get-PeMachine $binary
        if ($machine -ne $amd64Machine) {
            throw ('Expected AMD64 PE machine 0x8664, got 0x{0:x4}: {1}' -f $machine, $binary)
        }
    }

    $helpOutput = & $bridgeService --help 2>&1
    $helpText = $helpOutput -join "`n"
    if ($LASTEXITCODE -ne 0 -or $helpText -notmatch 'Convbased VST3 SDK bridge') {
        throw "The packaged bridge service failed its startup check: $helpText"
    }

    $validator = Join-Path $repoRoot 'build\bin\Release\validator.exe'
    if (-not (Test-Path -LiteralPath $validator)) {
        throw 'The Release VST3 validator is missing. Run scripts/build.ps1 first.'
    }
    $validatorOutput = & $validator $pluginBundle 2>&1
    if ($LASTEXITCODE -ne 0) {
        $validatorOutput | ForEach-Object { Write-Host $_ }
        throw "The packaged VST3 failed validator with exit code $LASTEXITCODE"
    }
    $validatorResult = $validatorOutput | Select-String -Pattern '^Result:' | Select-Object -Last 1

    $files = Get-ChildItem -LiteralPath $packageRoot -File -Recurse
    $size = ($files | Measure-Object Length -Sum).Sum
    Write-Host "Package smoke test passed: $($zip.FullName)"
    Write-Host "Files: $($files.Count); extracted bytes: $size"
    Write-Host "PE architecture: AMD64 (0x8664)"
    Write-Host "VST3 validator: $validatorResult"
} finally {
    if (Test-Path -LiteralPath $smokeRoot) {
        Remove-Item -LiteralPath $smokeRoot -Recurse -Force
    }
}

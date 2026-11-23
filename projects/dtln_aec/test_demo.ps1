# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0

param([switch]$Synthetic)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$testData = Join-Path $PSScriptRoot 'test_data'
$python = Join-Path $repoRoot '.venv/Scripts/python.exe'

if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
    throw 'Install uv and make it available on PATH.'
}
if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
    throw 'Set up the repository Python environment before running the demo test.'
}

$packRoot = $env:CMSIS_PACK_ROOT
if (-not $packRoot) {
    $environmentFile = Join-Path $repoRoot '.cmsis/tools-environment.yml'
    $packRootLine = Select-String -LiteralPath $environmentFile `
        -Pattern '^\s*CMSIS_PACK_ROOT:\s*(.+?)\s*$' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $packRootLine) {
        throw 'Set CMSIS_PACK_ROOT to the CMSIS pack directory used by VS Code.'
    }
    $packRoot = $packRootLine.Matches[0].Groups[1].Value.Trim('"', "'")
}

$sdsTools = Join-Path $packRoot 'ARM/SDS/3.1.0/utilities'
$sdsServer = Join-Path $sdsTools 'sdsio-server.py'
$sdsRequirements = Join-Path $sdsTools 'requirements.txt'
if (-not (Test-Path -LiteralPath $sdsServer -PathType Leaf) -or
    -not (Test-Path -LiteralPath $sdsRequirements -PathType Leaf)) {
    throw "Install ARM::SDS@3.1.0 through CMSIS Solution. Expected its utilities at $sdsTools."
}

if ($Synthetic) {
    $playbackConfig = Join-Path $testData 'synthetic.sdsio.yml'
    $inputDirectory = Join-Path $testData 'sds_run/synthetic'
    $labels = @(0)
    $comparison = @((Join-Path $testData 'validate_patterns.py'), 'compare', '--check')
} else {
    $playbackConfig = Join-Path $testData 'datatest.sdsio.yml'
    $inputDirectory = Join-Path $testData 'sds_run'
    $labels = @(0, 1, 2)
    $comparison = @((Join-Path $testData 'sds_demo.py'), 'compare', '--summary')
}

foreach ($label in $labels) {
    $inputFile = Join-Path $inputDirectory "Dtln_In.$label.sds"
    if (-not (Test-Path -LiteralPath $inputFile -PathType Leaf)) {
        throw "Missing $inputFile. Follow the input preparation steps in projects/dtln_aec/test_data/README.md."
    }
}

Push-Location -LiteralPath $repoRoot
try {
    $playbackStarted = [DateTime]::UtcNow
    & uv --cache-dir .uv-cache run --with-requirements $sdsRequirements python $sdsServer -c $playbackConfig -p -x
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    foreach ($label in $labels) {
        $capture = Get-Item -LiteralPath (Join-Path $inputDirectory "Dtln_Out.$label.p.sds") -ErrorAction SilentlyContinue
        if (-not $capture -or $capture.LastWriteTimeUtc -lt $playbackStarted) {
            throw "Playback did not produce a new capture for label $label in $inputDirectory."
        }
    }
    & $python @comparison
    exit $LASTEXITCODE
} finally {
    Pop-Location
}

# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0

param([switch]$Synthetic)

& (Join-Path $PSScriptRoot 'test_demo.ps1') @PSBoundParameters
exit $LASTEXITCODE

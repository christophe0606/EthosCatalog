param([string]$PackRoot = $env:CMSIS_PACK_ROOT,
      [ValidateSet('template_mram', 'template_ospi')][string]$Project = 'template_mram')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    New-Item -ItemType Directory -Force out | Out-Null
    $dsp = Join-Path $PackRoot 'ARM/CMSIS-DSP/1.18.0'
    $sources = @(
        'TransformFunctions/arm_rfft_fast_init_f32.c', 'TransformFunctions/arm_rfft_fast_f32.c',
        'TransformFunctions/arm_cfft_init_f32.c', 'TransformFunctions/arm_cfft_f32.c',
        'TransformFunctions/arm_cfft_radix8_f32.c', 'TransformFunctions/arm_bitreversal2.c',
        'CommonTables/arm_common_tables.c', 'CommonTables/arm_const_structs.c',
        'BasicMathFunctions/arm_scale_f32.c',
        'BasicMathFunctions/arm_mult_f32.c', 'BasicMathFunctions/arm_offset_f32.c',
        'StatisticsFunctions/arm_mean_f32.c', 'WindowFunctions/arm_hanning_f32.c',
        'SupportFunctions/arm_q15_to_float.c',
        'ComplexMathFunctions/arm_cmplx_mag_f32.c', 'SupportFunctions/arm_fill_q15.c'
    ) | ForEach-Object { Join-Path $dsp "Source/$_" }
    & clang -std=c11 -O3 -ffast-math -D_CRT_SECURE_NO_WARNINGS -I "projects/$Project" -I "projects/$Project/generated" -I common -I "$dsp/Include" -I "$dsp/PrivateInclude" -I "$PackRoot/ARM/CMSIS/6.3.0/CMSIS/Core/Include" tests/test_audio_nodes.c common/nodes_impl/c/sliding_buffer.c common/nodes_impl/c/window.c common/nodes_impl/c/fft.c "projects/$Project/spectrum_ui.c" @sources -o out/test_audio_nodes.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host compilation failed' }
    & ./out/test_audio_nodes.exe
    if ($LASTEXITCODE -ne 0) { throw 'Spectrum checks failed' }
} finally {
    Pop-Location
}

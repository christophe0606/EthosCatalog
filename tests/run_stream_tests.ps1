param([string]$PackRoot = $env:CMSIS_PACK_ROOT)
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    New-Item -ItemType Directory -Force out/stream-tests | Out-Null
    $includes = @('-Icommon', '-Icommon/stream', '-Icommon/nodes_impl/c', '-Iprojects/template_mram', '-Iprojects/template_mram/nodes_impl', '-Iprojects/template_mram/generated', "-I$PackRoot/ARM/CMSIS-DSP/1.18.0/Include", "-I$PackRoot/ARM/CMSIS/6.3.0/CMSIS/Core/Include")
    $dsp = Join-Path $PackRoot 'ARM/CMSIS-DSP/1.18.0'
    $sources = @('common/nodes_impl/c/sliding_buffer.c', 'common/nodes_impl/c/window.c', 'common/nodes_impl/c/fft.c')
    $sources += @(
        'TransformFunctions/arm_rfft_fast_init_f32.c', 'TransformFunctions/arm_rfft_fast_f32.c',
        'TransformFunctions/arm_cfft_init_f32.c', 'TransformFunctions/arm_cfft_f32.c',
        'TransformFunctions/arm_cfft_radix8_f32.c', 'TransformFunctions/arm_bitreversal2.c',
        'CommonTables/arm_common_tables.c', 'CommonTables/arm_const_structs.c',
        'BasicMathFunctions/arm_scale_f32.c', 'ComplexMathFunctions/arm_cmplx_mag_f32.c',
        'BasicMathFunctions/arm_mult_f32.c', 'BasicMathFunctions/arm_offset_f32.c',
        'StatisticsFunctions/arm_mean_f32.c', 'WindowFunctions/arm_hanning_f32.c',
        'SupportFunctions/arm_q15_to_float.c'
    ) | ForEach-Object { Join-Path $dsp "Source/$_" }
    $objects = @()
    foreach ($source in $sources) {
        $obj = "out/stream-tests/$([IO.Path]::GetFileNameWithoutExtension($source)).obj"
        $objects += $obj
        & clang -std=c11 -O2 @includes "-I$dsp/PrivateInclude" -c $source -o $obj
        if ($LASTEXITCODE -ne 0) { throw 'Host C compilation failed' }
    }
    & clang++ -std=c++17 -O2 -Itests/stream -Ithirdparty/CMSIS-Stream/platform -Ithirdparty/CMSIS-Stream/platform/posix_runtime @includes tests/test_stream_nodes.cpp @objects -o out/stream-tests/test_stream_nodes.exe
    if ($LASTEXITCODE -ne 0) { throw 'Stream node compilation failed' }
    & ./out/stream-tests/test_stream_nodes.exe
    if ($LASTEXITCODE -ne 0) { throw 'Stream node checks failed' }
} finally { Pop-Location }

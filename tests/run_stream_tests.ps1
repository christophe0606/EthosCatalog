param([string]$PackRoot = $env:CMSIS_PACK_ROOT)
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    New-Item -ItemType Directory -Force out/stream-tests | Out-Null
    $includes = @('-Icommon', '-Icommon/stream', '-Icommon/stream/nodes', '-Iprojects/template_mram', '-Iprojects/template_mram/generated', "-I$PackRoot/ARM/CMSIS-DSP/1.18.0/Include", "-I$PackRoot/ARM/CMSIS/6.3.0/CMSIS/Core/Include")
    foreach ($source in @('sliding_buffer','window')) {
        & clang -std=c11 -O2 @includes -c "common/nodes_impl/c/$source.c" -o "out/stream-tests/$source.obj"
        if ($LASTEXITCODE -ne 0) { throw 'Host C compilation failed' }
    }
    & clang++ -std=c++17 -O2 -Itests/stream -Ithirdparty/CMSIS-Stream/platform -Ithirdparty/CMSIS-Stream/platform/posix_runtime @includes tests/test_stream_nodes.cpp out/stream-tests/sliding_buffer.obj out/stream-tests/window.obj -o out/stream-tests/test_stream_nodes.exe
    if ($LASTEXITCODE -ne 0) { throw 'Stream node compilation failed' }
    & ./out/stream-tests/test_stream_nodes.exe
    if ($LASTEXITCODE -ne 0) { throw 'Stream node checks failed' }
} finally { Pop-Location }

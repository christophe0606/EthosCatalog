# Shared CMSIS Stream nodes

`../nodes/` contains Python descriptions, one class per file. `nodes/` contains
their C++ wrappers. Each wrapper calls the processing functions in
`common/nodes_impl`; it does not duplicate DSP or board-driver implementations.
`../stream_graph.py` applies the graph skill's shared scheduler conventions and
generates parameter structures, hardware setters, includes, JSON metadata,
DOT and (when Graphviz is available) PDF.

| Node | Dataflow input -> output | Parameters / state |
| --- | --- | --- |
| MicrophoneSource | none -> PcmBlock | Hardware callbacks; starts capture in the scheduler thread |
| PCMToFloat | PcmBlock -> AudioBlock | Signed PCM normalization |
| EmbeddedGain | AudioBlock -> AudioBlock | Shared EmbeddedModule and NPU-visible input memory in `hw_` |
| SlidingBuffer | AudioBlock -> FFTBlock | Owns history; discards it on discontinuity |
| RemoveDC | FFTBlock -> FFTBlock | Stateless |
| HannWindow | FFTBlock -> FFTBlock | Owns coefficients and calibrated amplitude scale |
| FFT | FFTBlock -> FFTBlock | Owns the existing 2048-point RFFT instance |
| Magnitude | FFTBlock -> SpectrumBlock | One-sided spectrum including DC/Nyquist |
| SendToLCD | SpectrumBlock -> event | Bounded payload storage and publication cadence |
| LCD | event -> none | Owns display/touch state; identified for external inspection |

Each FIFO transfers one typed packet per scheduler iteration. A packet carries
samples plus `StreamBlockInfo`: capture timing, discontinuity, validity and
amplitude scale. Packet array lengths come from the project's generated
`app_sizes.h`; Python `CStructType` sizes are checked by C++ static assertions.
Invalid warm-up packets continue through the fixed schedule without running
the FFT or publishing a partial spectrum. The fourth fresh 512-sample block
completes the 2048-sample history after startup or a gap.

`SendToLCD` bridges dataflow to the `spectrum` event selector. It preallocates
three Stream `TensorPtr<float>` payloads during initialization. Queued or
executing messages own references, so their payloads remain immutable. The
producer only fills an unreferenced slot, under its short buffer mutex, then
posts asynchronously. A full queue or busy pool drops a publication and counts
it. Rendering and VSYNC never run in the dataflow thread. The LCD event handler
copies the payload and releases its reference before drawing.

Projects provide `datatypes.h`, `hardware_params.h/.cpp` and `stream_init.cpp`.
The hardware block includes only the required peripherals, model and renderer
callbacks. ExecuTorch is hidden behind `common/embedded_gain.h` because its
platform enums conflict with Stream's global event names. Only the hardware
implementation and model boundary include ExecuTorch headers.

RTE configuration shims include the shared files in this folder. Runtime pool,
stack and priority settings come from the project's Python graph. The MRAM
graph uses the pack's CMSIS-RTOS dataflow and event threads. It runs continuously
until reset; this application does not switch between multiple graphs.

Regenerate the MRAM graph from the repository root:

```powershell
uv pip install -e ./thirdparty/CMSIS-Stream/PythonPackage
uv run projects/template_mram/app.py
```

From `projects/template_mram`, use `uv run app.py`. The generated sources and
metadata are identical from either directory. `dot -Tpdf` creates `app.pdf`
beside `app.dot`; missing Graphviz or a conversion error only emits a warning.

Host checks use production Stream FIFO/event classes and the production node
wrappers, with a host clock and the Stream POSIX platform:

```powershell
./tests/run_stream_tests.ps1 -PackRoot C:/ctools_pack
./tests/run_audio_tests.ps1 -PackRoot C:/ctools_pack
```

These check gap warm-up, overlap, normalization, window scale, immutable queued
spectra, bounded pool exhaustion, rejected publications, LCD event handling,
and the original DSP/renderer cases. Hardware validation is recorded in
`documentation/audio-spectrum-validation.md`.

# Shared CMSIS Stream nodes

`../nodes/` contains Python descriptions, one class per file. `nodes/` contains
their C++ wrappers. Each wrapper calls the processing functions in
`common/nodes_impl`; it does not duplicate DSP or board-driver implementations.
`../stream_graph.py` applies the graph skill's shared scheduler conventions and
generates parameter structures, hardware setters, includes, JSON metadata,
DOT and (when Graphviz is available) PDF.

| Node | Dataflow input -> output | Parameters / state |
| --- | --- | --- |
| MicrophoneSource | none -> `int16_t[n]` PCM | Hardware callbacks; reblocks capture into the configured sample count |
| PCMToFloat | `int16_t[n]` PCM -> `float[n]` audio | Signed PCM normalization |
| EthosGain | `float[n]` audio -> `float[n]` audio | Ethos-backed EmbeddedModule and NPU-visible input in `hw_`; n matches the model |
| SlidingBuffer | `float[hop]` audio -> `float[frame_samples]` audio | Owns zero-initialized history |
| RemoveDC | `float[n]` audio -> `float[n]` audio | Removes the frame mean |
| HannWindow | `float[n]` audio -> `float[n]` audio | Owns periodic Hann coefficients |
| FFT | `float[n]` audio -> `float[n]` packed RFFT | Power-of-two n from 32 to 4096 |
| Magnitude | `float[n]` packed RFFT -> `float[n/2+1]` bins | DC/Nyquist included; configurable amplitude scale |
| SendToLCD | `float[bins]` magnitudes -> event | Bounded payload storage and publication cadence |
| LCD | event -> none | Owns display/touch state; identified for external inspection |

The notation above describes scalar FIFO sample types and samples per firing;
arrays are not wrapped in block structs. Each Python constructor selects its
sample count. CMSIS Stream balances different compatible production/consumption
rates, sizes FIFOs, and repeats nodes as needed. For example, a 512-sample
microphone feeds a 256-sample `PCMToFloat` twice before a 512-sample `EthosGain`.
Coprime rates can create long schedules; adjust the node lengths in that case.

Node docstrings describe sample meanings and frame layouts. The graph author
must connect compatible meanings: audio, packed RFFT scalars and magnitudes all
use `float`, so type equality alone cannot validate a connection. The deployed
Ethos model still determines its tensor length; `EthosGain` checks the configured
length against hardware input storage during initialization.
`FFT` selects its size-specific CMSIS-DSP initializer with `if constexpr`, so
unused FFT sizes and their tables can be discarded by the linker.

The signal-processing graph assumes continuous realtime input. It carries no
packet headers, gap flags, warm-up validity or calibration metadata. Sliding
history starts with zeros and produces a full frame every firing; after four
512-sample hops all 2048 samples come from capture. `Magnitude` receives its
calibration from Python (`4/n` for the periodic Hann window), keeping it independent
of a particular window node. The microphone's hardware block size is independent
of its output rate: partial blocks are retained in storage allocated at init,
while whole blocks can be read directly into the FIFO.

`StreamStats` is diagnostic state outside the sample FIFOs. Audio block counts
count source firings; timing measures from the latest source firing to
`SendToLCD`. In the default graph this is one 512-sample audio hop. If a graph
batches several source firings before a sink firing, this counter measures from
the most recent source firing, rather than the age of every buffered sample.

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

These check sample reblocking, mixed 256/512 rates, zero-padded overlap,
normalization, the Ethos model interface, calibrated FFT amplitude, immutable
queued spectra, bounded pool exhaustion, rejected publications, LCD event handling,
and the original DSP/renderer cases. Generator rate-balancing checks run with
`uv run python -m unittest discover -s tests -p test_stream_graph.py`. Hardware validation is recorded in
`documentation/audio-spectrum-validation.md`.

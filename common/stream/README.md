# Shared CMSIS Stream nodes

`../nodes/` contains reusable Python descriptions, one class per file. Their
portable C++ wrappers live in `../nodes_impl/c/`, beside the processing functions
they call. Board-specific drivers live in `../nodes_impl/alif_e8/`;
`../nodes_impl/helium/` is reserved for implementations that require Helium.
This `stream/` folder holds shared runtime configuration and interfaces.
Each node header declares only its own C backend interface inside `extern "C"`;
`SlidingBuffer.hpp` also owns `sliding_buffer_t`. Shared support headers do not
expose unrelated processing functions. C backends include their owning node
headers with the C++ wrapper sections excluded during C compilation.
`../stream_graph.py` applies the graph skill's shared scheduler conventions and
generates parameter structures, hardware setters, includes, JSON metadata,
DOT and (when Graphviz is available) PDF.

| Node | Dataflow input -> output | Parameters / state |
| --- | --- | --- |
| MicrophoneSource | none -> `int16_t[n]` PCM | Hardware callbacks; reblocks capture into the configured sample count |
| PCMToFloat | `int16_t[n]` PCM -> `float[n]` audio | Signed PCM normalization |
| SlidingBuffer | `float[hop]` audio -> `float[frame_samples]` audio | Retains only `frame_samples - hop` zero-initialized overlap samples |
| RemoveDC | `float[n]` audio -> `float[n]` audio | Removes the frame mean |
| HannWindow | `float[n]` audio -> `float[n]` audio | Owns periodic Hann coefficients |
| FFT | `float[n]` audio -> `float[n]` packed RFFT | Power-of-two n from 32 to 4096 |
| RFFTMagnitude | `float[n]` CMSIS-DSP fast RFFT packing -> `float[n/2+1]` bins | DC/Nyquist included; configurable amplitude scale |
| SdsSource | SDS record -> `T[n]` | Read handle, optional timeslot output and retry policy |
| SdsSink | `T[n]` -> SDS record | Write handle, timeslot callback and optional retry policy |

Application nodes live in their project's `nodes/` and `nodes_impl/` folders.
Each template's `EthosGain`, `SendToLCD` and `LCD` belong there: its model
demonstration, spectrum event protocol, publication policy, drawing, animation
and touch interaction are specific to that application. Injecting hardware or
renderer callbacks does not make an application node reusable. Only the
UI-independent LCD presentation and touch driver remains shared. OSPI owns
separate copies of these application nodes and does not import MRAM's nodes.

The notation above describes scalar FIFO sample types and samples per firing;
arrays are not wrapped in block structs. Each Python constructor selects its
sample count. CMSIS Stream balances different compatible production/consumption
rates, sizes FIFOs, and repeats nodes as needed. For example, a 512-sample
microphone feeds a 256-sample `PCMToFloat` twice before a 512-sample `SlidingBuffer`.
Coprime rates can create long schedules; adjust the node lengths in that case.

Node docstrings describe sample meanings and frame layouts. The graph author
must connect compatible meanings: audio, packed RFFT scalars and magnitudes all
use `float`, so type equality alone cannot validate a connection.
`FFT` selects its size-specific CMSIS-DSP initializer with `if constexpr`, so
unused FFT sizes and their tables can be discarded by the linker.

The floating-point fast RFFT uses exactly `n` floats for `n/2+1` complex bins:
`[Re(X[0]), Re(X[n/2]), Re(X[1]), Im(X[1]), ...]`. DC and Nyquist are purely
real, so the Nyquist value occupies the otherwise unused imaginary slot of DC.
`RFFTMagnitude` handles those first two scalars separately and applies complex
magnitude only to the remaining `n/2-1` pairs. Its output has `n/2+1` real
magnitudes; one-sided amplitude scaling doubles only the interior bins.
This node requires the CMSIS-DSP fast floating-point RFFT packing. A general
CFFT spectrum needs a general complex-magnitude node. An unpacked RFFT result
must be explicitly converted to the required packed format before this node.
See the [CMSIS-DSP real FFT packing specification](https://arm-software.github.io/CMSIS-DSP/latest/group__RealFFT.html).

The signal-processing graph assumes continuous realtime input. It carries no
packet headers, gap flags, warm-up validity or calibration metadata. Sliding
history starts with zeros and produces a full frame every firing; after four
512-sample hops all 2048 samples come from capture. `RFFTMagnitude` receives its
calibration from Python (`4/n` for the periodic Hann window), keeping it independent
of a particular window node. The microphone's hardware block size is independent
of its output rate: partial blocks are retained in storage allocated at init,
while whole blocks can be read directly into the FIFO.

`SlidingBuffer` builds each frame in its output FIFO and retains only the tail
needed for the next firing, before downstream nodes can modify the output.
It stores no full-frame copy. With a 2048-sample frame and 512-sample hop, its
sample storage is 1536 floats (6 KiB), saving 2 KiB; equal hop/frame lengths
require no retained samples.

Each project's `stream_types.h` defines `StreamStats`, diagnostic state
outside the sample FIFOs. Audio block counts count source firings; timing
measures from the latest source firing to
`SendToLCD`. In the default graph this is one 512-sample audio hop. If a graph
batches several source firings before a sink firing, this counter measures from
the most recent source firing, rather than the age of every buffered sample.

`SendToLCD` bridges dataflow to the `spectrum` event selector. It preallocates
three Stream `TensorPtr<float>` payloads during initialization. Queued or
executing messages own references, so their payloads remain immutable. The
producer only fills an unreferenced slot, under its short buffer mutex, then
posts asynchronously. A full queue or busy pool drops a publication and counts
it. Rendering and VSYNC never run in the dataflow thread. The LCD event handler
copies the payload into its own retained spectrum buffer and releases its
reference before drawing. Its spectrum buffer and peak frequency are node
state, allocated during initialization rather than supplied by `HardwareParams`.
The renderer receives the magnitude array explicitly for each call.

Projects provide `datatypes.h`, `hardware_params.h/.cpp` and `stream_init.cpp`.
The hardware block includes only the required peripherals, model and renderer
callbacks. Each template's `EthosGain`, `SendToLCD` and `LCD` descriptions live
in its own `nodes/` folder, with their C++ wrappers and the gain's
ExecuTorch adapter in its `nodes_impl/` folder. The adapter keeps
ExecuTorch's platform enums out of Stream translation units, where they conflict with global event
names. Only the hardware implementation and model boundary include ExecuTorch
headers.

RTE configuration shims include the shared files in this folder. Runtime pool,
stack and priority settings come from the project's Python graph. Both template
graphs use the pack's CMSIS-RTOS dataflow and event threads. Each runs continuously
until reset; this application does not switch between multiple graphs.

Regenerate either graph from the repository root:

```powershell
uv pip install -e ./thirdparty/CMSIS-Stream/PythonPackage
uv run projects/template_mram/app.py
uv run projects/template_ospi/app.py
```

From either template folder, use `uv run app.py`. The generated sources and
metadata are identical from either directory. Graphviz creates `app.pdf` and a
PNG thumbnail, `app.png`, beside `app.dot` in `generated/`. The thumbnail fits
within 320 x 240 pixels and links to the PDF in the root README catalog.
Missing Graphviz or a conversion error only emits a warning.

Host checks use production Stream FIFO/event classes and the production node
wrappers, with a host clock and the Stream POSIX platform:

```powershell
./tests/run_stream_tests.ps1 -PackRoot C:/ctools_pack
./tests/run_audio_tests.ps1 -PackRoot C:/ctools_pack
./tests/run_stream_tests.ps1 -PackRoot C:/ctools_pack -Project template_ospi
./tests/run_audio_tests.ps1 -PackRoot C:/ctools_pack -Project template_ospi
```

These check sample reblocking, mixed 256/512 rates, zero-padded overlap,
normalization, the Ethos model interface, calibrated FFT amplitude, immutable
queued spectra, bounded pool exhaustion, rejected publications, LCD event handling,
and the original DSP/renderer cases. Generator rate-balancing checks run with
`uv run python -m unittest discover -s tests -p test_stream_graph.py`. Hardware validation is recorded in
`documentation/audio-spectrum-validation.md`.

## SDS playback and recording

`SdsSource(name, the_type, samples, params)` and `SdsSink(name, the_type,
samples, params)` take a CMSIS Stream type such as `CType(SINT16)` and a
positive sample count. They expose the dataflow ports `o` and `i`, respectively.
Each firing transfers one SDS record of `samples * sizeof(T)` bytes. Choose
trivially copyable sample types with a byte layout that matches the SDS file
metadata. Interleaved channels can share one record by including all channel
values in the count. CMSIS Stream can also reblock between different source
and sink rates.

`params` is a C++ constructor argument expression. For example, pass
`"replayParams"` or `"recordParams"` in Python and declare those variables as
`SdsSourceParams` and `SdsSinkParams` in the application's `app_config.hpp`
after including `SdsEndpoint.hpp`. The nodes use the ARM SDS 3.x Stream API.
The application chooses the SDSIO transport, initializes SDS and opens each
stream with `sdsModeRead` or `sdsModeWrite` before initializing the graph. Use
one node per handle and size the SDS stream buffer for at least a full record
including its SDS header. Keep the handles open until scheduling finishes,
then close them through SDS to flush pending capture data. A new set of stream
handles can be supplied when the application initializes the graph again.
[ARM SDS stream lifecycle](https://arm-software.github.io/SDS-Framework/main/theory.html)

The sink calls its application clock once per firing and uses the same
timeslot across retries. The source optionally reports the latest successfully
read timeslot to a `uint32_t*` in `SdsSourceParams`. The application supplies any
playback pacing. Both nodes leave the FIFO sample count unchanged until SDS
accepts or returns a complete record. The optional bounded retry policy lets
the application yield or wait on temporary starvation, backpressure, or SDS
lock contention. A wait callback may cancel by returning `false`. End of stream
and cancellation return `CG_STOP_SCHEDULER`. Size mismatches and SDS I/O errors
return `CG_BUFFER_ERROR`. With retries exhausted, `SDS_NO_DATA` returns
`CG_BUFFER_UNDERFLOW` and `SDS_NO_SPACE` returns `CG_BUFFER_OVERFLOW`.
`lastSdsStatus()` reports the most recent SDS API result. Choose a playback
record count that completes a schedule cycle when the graph reblocks data, or
handle any remaining samples in the application.

An optional record observer receives each successful record once on the
scheduler thread. Copy any data needed after the callback returns. Keep the
callback short to avoid delaying the scheduler.

The host check generates synchronous and asynchronous graphs, builds both
against the installed SDS pack header, and uses a test double for SDS I/O.
Run `uv run python tests/run_sds_tests.py` with `CMSIS_PACK_ROOT` set and a host
Clang compiler on `PATH`. On Windows, `--vcvars` accepts Visual Studio's
`vcvarsall.bat` to use MSVC.

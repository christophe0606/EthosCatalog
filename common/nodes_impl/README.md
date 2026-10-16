Reusable processing stages live here. Each Stream node owns its C backend
declarations in an `extern "C"` section of its C++ header, together with any
node-specific state types. The C implementation includes its owning header;
C++ runtime code is guarded so C compilation sees only that node's interface.
There is no public umbrella header exposing all processing functions. Audio
tests declare their required interfaces independently. Both templates use the
Stream wrappers rather than project-local raw processing declarations.

- `c/`: portable processing, CMSIS-DSP calls, and CMSIS Stream C++ wrappers.
  The Stream wrappers and their shared `NodeSupport.hpp` live beside the
  processing functions they call. The reusable microphone wrapper uses injected
  hardware callbacks and contains no board-specific code. CMSIS-DSP chooses its
  scalar or accelerated implementation at build time. `embedded_runtime.cpp`
  is necessarily C++ because EmbeddedModule is a C++ API; it has no board code.
- `helium/`: reserved for implementations that require Helium intrinsics.
  No node currently requires that restriction; the CMSIS-DSP build supplies
  acceleration on the M55 and remains usable on other supported cores.
- `alif_e8/`: PDM microphone, LCD, touch, pin, cache and power configuration.

The selected board layer supplies the board-specific driver interface and
hardware configuration. The DevKit-E8 microphone block size, LCD pixel count,
and driver declarations live in `board/DevKit-E8/board_nodes.h`, not in `common`.
Portable Stream wrappers receive their hardware callbacks and parameters from
the project adapter.

Common nodes must be reusable across projects; portability alone is insufficient.
The spectrum `LCD` and `SendToLCD` nodes live in
each template's `nodes_impl/`, alongside its model-specific `EthosGain`.
Spectrum rendering, animation, touch interaction and publication policy belong
to its project. The shared LCD driver only initializes the panel, presents
framebuffers and reports touch coordinates; it imposes no drawing or UI policy.

The project supplies model bytes and persistent method/scratch pools to
`initialize_runtime()` once. The returned module, its loader and allocators
live until reset. `runtime_memory_usage()` exposes allocator high-water marks
for the project benchmark. Only the application's audio thread calls `forward()` after
the startup self-test. Pool sizes and linker placement belong to each project.

DC removal, window multiplication, FFT and packed RFFT magnitude are separate
calls with caller-owned storage and no scheduling or UI state. Their
[CMSIS Stream wrappers](../stream/README.md) expose them individually in the
MRAM and OSPI graphs. The Stream sliding-buffer node builds frames directly in
its output FIFO and retains only the overlap between firings.

`window.c` uses CMSIS-DSP for Q15-to-float conversion, mean and offset for DC
removal, periodic Hann generation, and elementwise window multiplication.
Q15 conversion maps -32768 to -1 and 32767 to 32767/32768 exactly; this direction
requires no saturation. DC removal and window application support in-place
processing. Hann initialization returns the one-sided amplitude scale 2/sum(window).

`RFFTMagnitude.hpp` owns the packed-RFFT magnitude interface. Its Stream node
accepts CMSIS-DSP fast F32 RFFT output only: DC and Nyquist share the first
pair of floats. It is not a general complex-spectrum magnitude node.

The microphone is a single-consumer source of signed 512-sample blocks at
48 kHz. Its interrupt handler reserves one ring slot for the next capture,
drops new blocks when full, and signals the audio thread. Per-block sequence
numbers identify discontinuities. PDM overflow powers down and restarts capture
in the consumer thread. The template graphs assume continuous input and do not
propagate discontinuity metadata or reset their sliding history on a gap.

The LCD takes two caller-owned, 32-byte-aligned RGB565 buffers in DMA-visible
SRAM. `lcd_present()` cleans the submitted buffer and waits for two scanline
events before the old front buffer can be reused. Only the LCD thread presents
and polls touch. `lcd_init()` runs before audio starts, serializing enclave
services and peripheral setup.

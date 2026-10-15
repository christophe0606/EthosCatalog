Reusable processing stages live here and expose their interfaces in `common/`.

- `c/`: portable C processing and CMSIS-DSP wrappers. CMSIS-DSP chooses its
  scalar or accelerated implementation at build time. `embedded_runtime.cpp`
  is necessarily C++ because EmbeddedModule is a C++ API; it has no board code.
- `helium/`: reserved for implementations that require Helium intrinsics.
  No node currently requires that restriction; the CMSIS-DSP build supplies
  acceleration on the M55 and remains usable on other supported cores.
- `alif_e8/`: PDM microphone, LCD, touch, pin, cache and power configuration.

The project supplies model bytes and persistent method/scratch pools to
`initialize_runtime()` once. The returned module, its loader and allocators
live until reset. `runtime_memory_usage()` exposes allocator high-water marks
for the project benchmark. Only the application's audio thread calls `forward()` after
the startup self-test. Pool sizes and linker placement belong to each project.

Sliding buffer, DC removal, window multiplication, FFT and magnitude are
separate calls with caller-owned storage. They contain no scheduling or UI
state, so future CMSIS-Stream nodes can wrap them individually.

The microphone is a single-consumer source of signed 512-sample blocks at
48 kHz. Its interrupt handler reserves one ring slot for the next capture,
drops new blocks when full, and signals the audio thread. Per-block sequence
numbers identify discontinuities. PDM overflow powers down and restarts capture
in the consumer thread; a gap resets the sliding window before further FFTs.

The LCD takes two caller-owned, 32-byte-aligned RGB565 buffers in DMA-visible
SRAM. `lcd_present()` cleans the submitted buffer and waits for two scanline
events before the old front buffer can be reused. Only the LCD thread presents
and polls touch. `lcd_init()` runs before audio starts, serializing enclave
services and peripheral setup.

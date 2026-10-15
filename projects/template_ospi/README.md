# OSPI template

This template runs the same microphone -> Ethos 2x gain -> spectrum application
as the [MRAM template](../template_mram/README.md). The application and drawing
stay in this project. Reusable processing and board nodes live in `../../common`.
Audio uses CMSIS Realtime priority; LCD rendering and animation use High priority.

Application code remains in MRAM. `flash_ospi: true` stores the PTE separately
in OSPI1 at `0xC0000000`, within a reserved 32 MiB window. Startup enables XIP,
checks the generated full-PTE CRC32 in place, and passes the OSPI address to the
persistent EmbeddedModule. The PTE is never copied into SRAM. U85 command and
constant reads use AXI_EXT; writable activations, scratch and input use SRAM0.
The two RGB565 LCD buffers also use SRAM0.

The flash uses continuous reads (volatile configuration register `07h = FFh`)
so NPU bursts can cross 32-byte boundaries. The CPU's OSPI1 MPU region uses
Normal non-cacheable memory (MAIR attribute `44h`), avoiding cache-line wrap
transactions against that continuous flash configuration. Other CPU memory
regions retain the board's cache policy. U85 command and weight accesses use
`NPU_MEM_ATTR_2/3=0x24` (Normal non-cacheable, non-bufferable on `AXI_EXT`).
The ISSI wrap settings are documented in the
[IS25WX256 datasheet](https://issi.com.cn/WW/pdf/25LX-WX256-128.pdf),
Rev. A14, May 12, 2026, page 24, Table 6.6.

| Target set | Build context | Behavior |
| --- | --- | --- |
| `DevKit-E8@OSPI_Debug` | `template_ospi.Debug` | Initialize XIP, verify model, start microphone spectrum |
| `DevKit-E8@OSPI_Release` | `template_ospi.Release` | Same application with optimization |
| `DevKit-E8@ProgramModel` | `template_ospi.ProgramModel` | Prepare hardware and wait; no model or inference runtime |

## Generate and build

1. In a terminal at the repository root, generate the OSPI layer:

   ```powershell
   uv run scripts/create_ai_layer.py --project template_ospi
   ```

2. Select **DevKit-E8@OSPI_Release** in the CMSIS UI.
3. Run **CMSIS: Build Solution** from the Command Palette.
4. To build the programming preparation firmware, select
   **DevKit-E8@ProgramModel** and run **CMSIS: Build Solution** again.

`ProgramModel` defines `MODEL_PROGRAMMING_MODE=1`. The same project supplies a
different startup path, so no separate preparation-firmware project is needed.
This build excludes the AI layer and inference sources and can be built before
model generation. It never reads the external model or starts the scheduler.

## What the preparation code does

`ospi_startup.c` overrides the board's weak `app_prepare()` hook. The board calls
it after initialization and before creating the application thread. In
`ProgramModel`, it checks enclave service results, enables the necessary clocks
and memory blocks, enables the OSPI1 clock, disables retained XIP, prepares flash
pins/reset, and disables interrupts, SysTick, caches and the MPU. It clears MSP
and PSP stack limits before publishing readiness and waiting in internal MRAM.

Before programming, stop at `model_programming_wait` and require:

```text
model_programming_ready = 1
app_stage = 5
app_error = 0
app_service_error = 0
```

The MRAM preparation path uses the vendor `ospi_init()` to disable retained XIP
before resetting the external flash. A core reset alone may retain XIP state;
no power cycle is needed for this handoff. `app_error = -305` means XIP remained
enabled despite preparation and programming must not proceed. After readiness
is established, preserve the hardware state without reset or power cycling until
programming completes.

The preparation application only waits. The separate source-built flash loader
under `board/flash/loader` supplies the erase/write algorithm that the `flash`
tool executes from RAM. Free the debug probe before calling that tool, and use
the generated model-only run description with pre/post resets disabled. See
the [Alif DevKit-E8 OSPI programming procedure](../../documentation/alif-e8-flashing.md#program-an-ospi-model).

For normal application startup, choose **OSPI_Debug** or **OSPI_Release** and
rebuild so the application run file selects that build instead of `ProgramModel`.
The normal path initializes XIP and sets the CPU's OSPI1 region non-cacheable
before accessing the programmed model. `model_verified == 1` records a
matching full-PTE CRC32. `gain_test_passed == 1` records the signed gain
self-test; advancing `audio_blocks` and `frames_presented` with
`pipeline_error == 0` prove the ongoing application is working.

## Validation

See [current audio validation](../../documentation/audio-spectrum-validation.md).
[model/benchmark.yml](model/benchmark.yml) and the accompanying raw UART capture
record ten measured, checked gain-network forwards after one warmup. The median
NPU time is 0.0235875 ms and median `module.forward()` time is 0.44615125 ms
at 400 MHz. The arena is 1216 bytes; method/temp peaks are 3752/1216 bytes.
Timing excludes capture, FFT, display and UART; the record also lists full
firmware memory allocations, including the two LCD buffers.
The compiled PTE has changed: program it separately before loading the new
application, or its CRC check will reject the old external model.

To derive a project, keep `ospi_startup.c`, the OSPI component and helper sources,
the SRAM inference-pool linker section and the `ProgramModel` context filters. Adapt
the model/quantizer/gain self-test constants and add the new project to the desired target sets
as described in [Add another project](../../README.md#add-another-project).

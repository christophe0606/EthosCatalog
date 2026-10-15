# OSPI template

This template uses the MRAM template's small convolution/ReLU model and fixed
test data. Application code remains in MRAM. `flash_ospi: true` stores the PTE
separately in OSPI1 at `0xC0000000`, within a reserved 32 MiB window.

The normal application enables XIP, checks the PTE's generated CRC32 in place
and passes its OSPI address directly to ExecuTorch's `BufferDataLoader`.
The U85 reads the command stream and constant weights through `AXI_EXT`
(`NPU_QCONFIG=2`, `NPU_REGIONCFG_0=3`). Writable activations, scratch and runtime
pools remain in SRAM0 (`NPU_REGIONCFG_1=0`); no SRAM model buffer is reserved
and the PTE is never staged there. Application code remains in MRAM.

The model has been programmed and checked inferences have passed on DevKit-E8;
see the hardware validation record below. The runner validates one warmup and
ten measured inferences, matching the MRAM template's measurement method.
[model/benchmark.yml](model/benchmark.yml) records the final run, with raw UART
output in [model/benchmark_uart.txt](model/benchmark_uart.txt).

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
| `DevKit-E8@OSPI_Debug` | `template_ospi.Debug` | Initialize XIP, verify model in place, benchmark checked inferences |
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

The implementation is adapted from `M55_HP/main.c` in the local reference
repository `Blinky_M55_HP`, branch `chess`, commit
`37c18e4eb6e32f46d467aafc9d3188f4c5b1f519`. The reference's complete board/enclave
initialization order and programming-entry operations are retained; chess,
diagnostic erase and benchmark code are excluded. Source copyright/license
notices are preserved. The existing board OSPI helpers are reused.

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
the root [step-by-step OSPI procedure](../../README.md#manually-program-a-future-ospi-network).

For normal application startup, choose **OSPI_Debug** or **OSPI_Release** and
rebuild so the application run file selects that build instead of `ProgramModel`.
The normal path initializes XIP and sets the CPU's OSPI1 region non-cacheable
before accessing the programmed model. `model_verified == 1` records a
matching full-PTE CRC32; `app_stage == 4` and `app_result == 0` record a completed
successful inference.

## Hardware validation

On October 15, 2026, `ProgramModel` prepared DevKit-E8, the model-only flash
operation programmed the PTE, and `OSPI_Release` built, loaded and completed one
inference. UART4 at 115200 baud reported `Test_result: PASS`, with 128 outputs
and a maximum absolute error of `0.005359` against the reference test data.

| Check | Observed result |
| --- | --- |
| PTE | 3104 bytes at `0xC0000000`, CRC32 `0xF0A854FB` |
| Firmware status | `app_stage=4`, `app_error=0`, `app_result=0`, `model_verified=1` |
| NPU completion | `npu_error=0`, `npu_interrupts=1`, `QREAD=QSIZE=300` |
| NPU command stream | `QBASE=0xC00000D0` |
| NPU constant weights | `BASEP0=0xC0000220`, 256 bytes |
| NPU writable arena | `BASEP1=0x02010000`, 192 bytes in SRAM0 |
| SRAM0 linker allocation | 131328 bytes: method pool, temporary pool and input; no PTE buffer |

The original 32-byte flash wrap configuration stalled the NPU command stream.
Continuous reads resolved that stall; making CPU OSPI1 accesses non-cacheable
also resolved the subsequent CPU metadata-access fault. The recorded pass uses
both settings. This initial check validated functional execution of the supplied
smoke model.

The subsequent Release benchmark used the same direct OSPI configuration and
passed all eleven checked inferences. With CPU and NPU clocks at 400 MHz and
OSPI configured at 25 MHz, the ten measured samples had a median NPU time of
`0.0204675 ms` (8187 cycles) and a median `module.forward()` time of
`0.394775 ms`. NPU timing excludes CPU work and IRQ overhead; forward timing
includes CPU work and PMU hooks. These measurements apply to this smoke model
and configuration.

To derive a project, keep `ospi_startup.c`, the OSPI component and helper sources,
the SRAM inference-pool linker section and the `ProgramModel` context filters. Adapt
the model/quantizer/test data and add the new project to the desired target sets
as described in [Add another project](../../README.md#add-another-project).

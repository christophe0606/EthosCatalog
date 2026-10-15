# OSPI template

This template uses the MRAM template's small convolution/ReLU model and fixed
test data. Application code remains in MRAM. `flash_ospi: true` stores the PTE
separately in OSPI1 at `0xC0000000`, within a reserved 32 MiB window.

The normal application enables XIP, copies the PTE into SRAM0, checks its
generated CRC32 and performs one checked inference. The reference chess
application also stages its model in SRAM because direct NPU reads from OSPI
stalled there. This template retains that placement; direct NPU execution from
OSPI is not assumed. Model staging plus inference pools must fit SRAM0.

There is no inference timing loop or OSPI benchmark record. Validation of this
template is limited to host generation, configuration checks and compilation;
it has not been loaded, flashed, run or benchmarked on the board.

| Target set | Build context | Behavior |
| --- | --- | --- |
| `DevKit-E8@OSPI_Debug` | `template_ospi.Debug` | Initialize XIP, verify/stage model, one inference |
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
and memory blocks, enables the OSPI1 clock before checking XIP, prepares flash
pins/reset, and disables interrupts, SysTick, caches and the MPU. It clears MSP
and PSP stack limits before publishing readiness and waiting in internal MRAM.

The implementation is adapted from `M55_HP/main.c` in the local reference
repository `Blinky_M55_HP`, branch `chess`, commit
`37c18e4eb6e32f46d467aafc9d3188f4c5b1f519`. The reference's complete board/enclave
initialization order and programming-entry operations are retained; chess,
diagnostic erase and benchmark code are excluded. Source copyright/license
notices are preserved. The existing board OSPI helpers are reused.

For later hardware use, stop at `model_programming_wait` and require:

```text
model_programming_ready = 1
app_stage = 5
app_error = 0
app_service_error = 0
```

`app_error = -305` means the controller still has XIP enabled. Following the
reference procedure, load the `ProgramModel` application into MRAM first, stop
debug/Run sessions, power-cycle the board, then reconnect to this preparation
firmware and verify readiness again. A core reset alone may retain XIP state.
This cold start is performed before verifying readiness; after readiness is
established, preserve the hardware state without reset or power cycling until
programming completes.

The preparation application only waits. The separate source-built flash loader
under `board/flash/loader` supplies the erase/write algorithm that the `flash`
tool executes from RAM. Free the debug probe before calling that tool, and use
the generated model-only run description with pre/post resets disabled. See
the root [step-by-step OSPI procedure](../../README.md#manually-program-a-future-ospi-network).

For normal application startup, choose **OSPI_Debug** or **OSPI_Release** and
rebuild so the application run file selects that build instead of `ProgramModel`.
The normal path preserves the runtime's MPU/cache/stack policy and initializes
XIP before accessing the programmed model. `model_verified == 1` records a
matching full-PTE CRC32; `app_stage == 4` and `app_result == 0` record a completed
successful inference. These checks have not been exercised on this template's
hardware.

To derive a project, keep `ospi_startup.c`, the OSPI component and helper sources,
the SRAM staging linker section and the `ProgramModel` context filters. Adapt
the model/quantizer/test data and add the new project to the desired target sets
as described in [Add another project](../../README.md#add-another-project).

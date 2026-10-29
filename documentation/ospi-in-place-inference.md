# Run an Ethos-U85 model directly from OSPI

Use this configuration on Alif DevKit-E8 when the model should remain in OSPI
and SRAM should hold writable inference data and application buffers.
Verified references are [template_ospi](../projects/template_ospi/README.md)
and [handwriting_synthesis](../projects/handwriting_synthesis/README.md).
The [YAMNet benchmark](../projects/yamnet/model/benchmark.yml) also verifies a
3,601,536-byte PTE directly in OSPI at 100 MHz, with 14 NPU calls per forward.
The speech projects instead copy their PTE into SRAM; use the direct examples
when deriving a project that needs in-place OSPI execution.

## Compiler and project settings

Start with `projects/template_ospi`, retaining its `ProgramModel` context and
context filters. Keep the new project's own model, quantizer and cache paths.
The OSPI portion of its `model/network.yml` is:

```yaml
network:
  # Other required model and quantization fields belong here.
  flash_ospi: true
  ospi:
    address: 0xC0000000
    size: 0x02000000
    pname: M55_HP
  vela:
    ini: ../../.cmsis/ensemble_vela.ini
    system_config: Ethos_U85_SRAM_OSPI
    memory_mode: Shared_Sram
```

This example reserves 32 MiB at OSPI1. Check that the PTE and any model assets
fit the partition and the solution's flash-algorithm coverage. Regenerate the
application's own `ai_layer/` after changing these settings. Its generated
`model_pte.c` must expose the external address rather than embed a model array.

The cproject settings used by the direct-OSPI examples are:

```yaml
project:
  define:
    - NPU_QCONFIG: 2
    - NPU_REGIONCFG_0: 3
    - NPU_REGIONCFG_1: 0
    - NPU_MEM_ATTR_2: 0x24
    - NPU_MEM_ATTR_3: 0x24
    - APP_MODEL_OSPI
    - APP_POOL_SECTION: \".bss.ai_pool\"
  components:
    - component: BSP:OSPI FLASH XIP:core
  # Retain the other application components and the Board/AI layers.
```

Keep `board/DevKit-E8/Board_HP-U85.clayer.yml`. These settings select the
external path for command streams and constants, with the writable arena in
SRAM. Do not substitute the speech projects' `NPU_MEM_ATTR_2/3: 0x20` settings:
those applications execute copied model data from SRAM. Size method/temp pools
from measured high-water marks. Retain the template linker scripts' SRAM0
`UNINIT` region for `.bss.ai_pool` and LCD framebuffers. The writable NPU arena
must be in NPU-visible memory, not Cortex-M local TCM.

## Startup and runtime

Keep the template's project-owned `ospi_startup.c` and the shared
`board/DevKit-E8/model_storage.c` and `e8_issi_flash.c` sources in the cproject.
The normal `app_prepare()` calls `model_storage_init()` before reading the PTE.
The programming path prepares the controller and waits without model reads or
inference.

`model_storage_init()` configures pins/reset and XIP, then makes the CPU OSPI
window non-cacheable. Preserve that MPU step: the board code documents that
CPU cache fills can request WRAP transactions while the NPU uses the linear
flash configuration. Enabling XIP alone does not reproduce this setup.

In `hardware_params_init()`, check the **entire PTE CRC32 in place** against the
generated `MODEL_PTE_CRC32`, then pass `model_pte` and `model_pte_size` directly
to `arm::embedded::initialize_runtime()`. Keep method/temp pools and input
tensors in SRAM0. Do not allocate a PTE-sized SRAM buffer or copy the PTE.
Retain the shared board cache-maintenance callbacks for writable SRAM tensors.

The handwriting project calls the guarded `e8_flash_set_read_clock()` API
before CRC/runtime initialization. Its validated read setting is 100 MHz at
400 MHz AXI, divider 4; the template uses the default 25 MHz. Keep programming
preparation at its safe default. When changing the read clock, use that API,
check its return code and divider readback, repeat CRC/output tests, and record
the actual clock in the benchmark. Compiler bandwidth is an estimate.

## Program and verify

Follow the complete [OSPI programming procedure](alif-e8-flashing.md#program-an-ospi-model).
The sequence to preserve is:

1. Generate the selected project's AI layer and model-only programming run file.
   Select the project explicitly when using the Python generators.
2. Build/load its `ProgramModel` target through CMSIS. At
   `model_programming_wait`, require `model_programming_ready == 1`,
   `app_stage == 5`, `app_error == 0`, and `app_service_error == 0`.
3. Detach and release the probe without resetting or power-cycling the prepared
   board. Call the CMSIS `flash` tool with the generated model-only run file.
   Wait for explicit successful completion; obtain final status when a tool
   reports work still running. A short wait or a generated run file is not
   evidence of successful programming.
4. Build/load the normal Release target with matching ELF/debugger symbols.
   Require the full in-place PTE CRC before accepting inference results.
5. Compare outputs with independent fixtures, require completed NPU PMU activity
   without overflow, and record pool peaks and clocks. Command/weight addresses
   should be in OSPI and the arena in SRAM0. Confirm linked SRAM usage contains
   no model-sized copy.

When diagnosing a first-inference stall, allow debugger startup and its resets
to finish before starting the NPU. The handwriting benchmark history records
a temporary startup gate for this purpose. A breakpoint before the first call
can provide the same check. Remove diagnostic gating for the final autonomous
application and verify a fresh normal launch. A partial command-read position
alone does not prove an operator or external-memory fault.

A failed CRC means no valid model execution was tested: fix programming or
startup first. Keep compiler estimates separate from board measurements.

## Source locations

| Concern | Reference |
| --- | --- |
| Direct OSPI project settings | `projects/template_ospi/template_ospi.cproject.yml` |
| In-place runtime initialization and CRC | `projects/template_ospi/hardware_params.cpp` |
| Preparation and normal startup | `projects/template_ospi/ospi_startup.c` |
| XIP and CPU MPU policy | `board/DevKit-E8/model_storage.c` |
| Guarded read-clock API | `board/DevKit-E8/e8_issi_flash.c` |
| Faster direct-OSPI example | `projects/handwriting_synthesis/hardware_params.cpp` |
| Writable-tensor cache hooks | `board/DevKit-E8/ethosu_cb_dcache.c` |
| Compiler memory descriptions | `.cmsis/ensemble_vela.ini` |

Preserve notices when reusing project-owned sources. Do not modify imported
packs or libraries to reproduce this configuration.

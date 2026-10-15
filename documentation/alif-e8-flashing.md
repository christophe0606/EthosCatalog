# Alif DevKit-E8 flashing and running

This procedure applies to the Alif DevKit-E8 M55 HP core. The flash algorithm,
OSPI initialization and programming handoff are specific to this board.

Complete [project selection and network preparation](../README.md#select-a-project-and-prepare-its-network)
first. All file paths and terminal commands below are relative to the repository
root.

- [Load and run an MRAM application](#load-and-run-an-mram-application)
- [Program an OSPI model](#program-an-ospi-model)

## Load and run an MRAM application

1. Connect the board's power, debug probe and UART connection. Install the Alif
   M55_HP debug stub/ATOC if required by your board; see
   [the board setup](../board/DevKit-E8/README.md).
2. Select the desired template target and build it after generating the AI layer.
3. Use CMSIS **Load & Debug** or **Load & Run** to program the application into
   MRAM. Its model is included in the application image; `flash_ospi: false`
   means there is no separate model programming step. The template's own
   `linker_ac6.sct.src` places the PTE/constant data in MRAM and inference pools
   in NPU-accessible SRAM0.
   The project's driver settings select EXT for the command stream/constants
   and SRAM for the arena (Ethos-U85 TRM `arm/102685-latest`, r0p0 Issue 05,
   pp.48–49 and 56–59; MEM_ATTR defaults in ethos-u-core-driver 1.26.5).
4. Set SW4 to U4 for UART4 output. Run the application and inspect stdout at
   115200 baud, 8 data bits, no parity, 1 stop bit. Expect `Ethos gain self-test: PASS`, then a live microphone spectrum on the
   standard MIPI LCD. Verify `gain_test_passed == 1`, `pipeline_error == 0`, and
   advancing `audio_blocks`/`frames_presented` in the debugger.

## Program an OSPI model

For an OSPI project, set `flash_ospi: true` and add its reserved partition:

```yaml
network:
  # ... name, model, safetensors, graph and vela as in the template ...
  flash_ospi: true
  ospi:
    address: 0xC0000000
    size: 0x02000000
    pname: M55_HP
```

The project must own a linker script and runtime storage initialization that
match its Vela configuration. The generated C interface refers to the OSPI
address and does not embed the PTE in the application. Declare that memory in
the solution and select an appropriate flash algorithm. The DevKit-E8 helpers
`board/DevKit-E8/model_storage.c` and `e8_issi_flash.c` are available for OSPI1
pin/reset/XIP setup. The OSPI template links them; the MRAM template does not.

**What "programming handoff" means**

The external OSPI flash chip needs to be initialized before the flash tool can
write the compiled model into it. In this repository's current programming
workflow, your application first runs some preparation code on the board. Then
the debugger takes control of the same processor and runs the flash loader from
RAM to write the model. **"Handoff" means this change of control from your
application's preparation code to the debugger's flash loader.**

The OSPI template already implements the preparation path below in
`projects/template_ospi/ospi_startup.c`. Its `app_prepare()` hook
runs after board initialization and before the RTOS scheduler starts. When
creating an OSPI project from this template, retain this path and adapt it only
where the board or memory requirements differ. It:

- Runs from internal MRAM or RAM, without reading the model from OSPI flash.
- Enables the required power and clocks, configures the OSPI pins and resets
  the external flash chip so it can accept programming commands.
- Leaves XIP disabled. XIP is the mode that lets the processor read the external
  flash through ordinary memory addresses; inference will need it later, after
  the model has been written.
- Disables the caches and MPU (memory protection unit), and clears the CPU's
  stack-limit settings before the RAM flash loader runs, as required by this
  loader's calling assumptions.
- Reaches a known wait point **after preparation has completed and before
  inference starts**. The user sets a debugger breakpoint there and halts the
  processor. If detaching the debugger resumes execution, this preparation
  path must keep waiting without changing the prepared settings or starting
  inference, until the flash tool takes control.

Select **DevKit-E8@ProgramModel** to build this preparation mode. It defines
`MODEL_PROGRAMMING_MODE=1`, does not start the scheduler, and does not link the
generated AI layer or inference code. Set a breakpoint at
`model_programming_wait`. Readiness requires `model_programming_ready == 1`,
`app_stage == 5`, `app_error == 0` and `app_service_error == 0`.
The MRAM preparation path disables retained XIP before resetting the flash.
`app_error == -305` means XIP remained enabled despite preparation; do not
program until readiness is established. The normal **OSPI_Debug/OSPI_Release** builds initialize
XIP and continue into the application. Both modes belong to the same project;
no separate preparation-firmware project is needed.

The existing `model_storage_prepare_programming()` helper in
`board/DevKit-E8/model_storage.c` disables retained XIP, configures pins and resets the external chip;
it supplies only part of this preparation. The OSPI template's `app_prepare()`
adds the remaining initialization, CPU settings and wait point. The same file's
`model_storage_init()` also enables XIP for normal model access, so it serves a
different stage of the workflow.

Setting `flash_ospi: true` and running `create_model_flash.py` only describe what
to program; they do not add or run this board preparation code. The generated
model-only run file attaches without resetting the board, so the flash loader
can use the settings that the application has already established. Resetting
or power-cycling the board at this point would lose that preparation.

1. Generate the selected project's AI layer and build its firmware with CMSIS.
2. Prepare a **model-only** programming description from its application run
   file (use the path CMSIS generated for your selected target):

   ```powershell
   uv run scripts/create_model_flash.py --run out/EthosCatalog+DevKit-E8.cbuild-run.yml --algorithm board/flash/DevKit-E8_IS25WX.FLM
   ```

   The output defaults to `out/<project>-model.cbuild-run.yml`. The script
   reads the selected project and checks its partition, generation hashes and
   programming algorithm. It never connects to the board. For the MRAM template it
   prints that MRAM application loading is sufficient and writes no flash file.
3. Run the application's board-preparation code described above, then halt the
   processor at its wait point before inference. Check that power/clocks and
   OSPI pins/reset are initialized, XIP and caches/MPU are disabled, and stack
   limits are cleared before the RAM flash algorithm runs. This preparation
   code and wait point are what this guide calls the "programming handoff";
   the OSPI template implements them in its `ProgramModel` configuration. The
   generated file uses attach with pre/post reset disabled to preserve that
   preparation.
4. Use the CMSIS Developer Assistant `flash` tool on the generated model-only
   run file. Then load and run the application's normal image with CMSIS.
   Configure OSPI XIP before inference. Repeat separate network programming
   whenever the compiled PTE changes.

The source flash loader is kept as a separate build-only solution; see
[its instructions](../board/flash/loader/README.md). The supplied template's OSPI
programming and inference have been verified on DevKit-E8; repeat the model
CRC and output checks when adapting it to another network.

### Hands-on sequence

This sequence uses the supplied `template_ospi`. For a derived OSPI project,
substitute its project/context names and retain the `ProgramModel` build mode.
Complete the [Python/tool setup](../README.md#select-a-project-and-prepare-its-network)
first. This preparation, model-only programming and inference sequence has
been exercised with the supplied template on DevKit-E8.

1. In VS Code, open the repository root folder. Choose **Terminal > New Terminal**
   and use a PowerShell terminal at that root. Run all commands below there.
2. Open `projects/template_ospi/model/network.yml`. Set `flash_ospi: true` and add
   the `ospi` mapping shown above, using the partition reserved for your model.
   Save the file.
3. In the same file, set `vela.system_config` and `vela.memory_mode` to match
   your project's OSPI memory placement. Save the file.
4. Open `projects/template_ospi/ospi_startup.c` to identify the provided
   preparation code and `model_programming_wait` breakpoint. The `ProgramModel`
   configuration runs this code without accessing the OSPI model. The normal
   configurations enable XIP; `app_main.cpp` verifies the model's generated
   CRC32 in place before starting the microphone spectrum. Writable
   inference/runtime pools and LCD framebuffers use SRAM0; the PTE is not copied there.
5. In Explorer, right-click
   `board/flash/loader/FlashLoader.csolution.yml` and choose
   **CMSIS: Select as Active Solution**. Select its **DevKit-E8** target in the
   CMSIS view. The only build context is `FlashLoader.FlashLoader+DevKit-E8`.
6. Open the Command Palette (**Ctrl+Shift+P**) and run **CMSIS: Build Solution**.
   Wait for a successful build. This builds the flash algorithm only.
7. In the repository-root terminal, publish the built algorithm:

   ```powershell
   uv run scripts/publish_e8_flash_loader.py
   ```

   Confirm that it prints `Published` for `board/flash/DevKit-E8_IS25WX.FLM`.
8. In Explorer, right-click `EthosCatalog.csolution.yml` and choose
   **CMSIS: Select as Active Solution** to return to the application solution.
9. Open **CMSIS: Manage Solution Settings** from the Command Palette. Select
   **DevKit-E8@OSPI_Release** and select only `template_ospi` for build, using its
   `Release` build type. Save/apply the selection before generating the layer.
10. Generate the selected application's AI layer:

    ```powershell
    uv run scripts/create_ai_layer.py
    ```

    The VS Code alternative is **Terminal > Run Task > Model: Create AI layer**.
    Confirm that `[ai_layer] selected project:` names `template_ospi.cproject.yml`.
    If the model or quantization policy changed, use **Model: Requantize and
    create AI layer**, or run `uv run scripts/create_ai_layer.py --quantize`
    instead.
11. Run **CMSIS: Build Solution** from the Command Palette. Wait for success.
12. Open the generated `out/EthosCatalog+DevKit-E8.cbuild-run.yml`. Confirm that
    its `programming` list contains an algorithm whose `start` and `size` cover
    the entire configured OSPI partition. If it does not, complete the board's
    memory/flash-algorithm configuration and rebuild before continuing.
    `--algorithm` in the next command replaces an existing covering algorithm;
    it does not create a missing programming entry.
13. Prepare the model-only programming file:

    ```powershell
    uv run scripts/create_model_flash.py --run out/EthosCatalog+DevKit-E8.cbuild-run.yml --algorithm board/flash/DevKit-E8_IS25WX.FLM
    ```

    Confirm that it prints `Prepared` for `out/template_ospi-model.cbuild-run.yml`.
    Use the actual application run-file path if your build generated a different
    one. Do not proceed with a file left over from a different project.
14. In **CMSIS: Manage Solution Settings**, select **DevKit-E8@ProgramModel**.
    It selects `template_ospi.ProgramModel` with `MODEL_PROGRAMMING_MODE=1`.
15. Run **CMSIS: Build Solution** and wait for success. This configuration does
    not need a generated AI layer and cannot start inference.
16. Connect the powered board and debug probe. Complete the debug-stub/ATOC
    setup in [the board instructions](../board/DevKit-E8/README.md) if needed.
17. Run **CMSIS: Load & Debug Application** from the Command Palette. Set a
    breakpoint at `model_programming_wait`, then continue to that breakpoint.
    Confirm `model_programming_ready == 1`, `app_stage == 5`, `app_error == 0`
    and `app_service_error == 0`. If `app_error == -305`, XIP remained enabled
    despite preparation; resolve it and verify readiness before programming. The flash
    tool will run the separate RAM flash loader after this preparation.
18. Run **CMSIS: Detach Debugger** to release the probe, following the project's
    procedure for preserving the prepared hardware state. If a separate CMSIS
    Run task still holds the probe, use **CMSIS: Stop Running** as directed by
    that procedure. The `flash` tool requires the probe to be free; do not reset
    or power-cycle the board between preparation and programming.
19. In the CMSIS Developer Assistant chat, request this tool action (this is
    a chat instruction, not a terminal command):

    ```text
    Use the flash tool with cbuildRunFile set to out/template_ospi-model.cbuild-run.yml. Wait for programming to complete and report the result.
    ```

    Confirm that programming succeeds before continuing. Supplying this path
    explicitly selects the model-only file rather than the normal application
    run file.
20. In **CMSIS: Manage Solution Settings**, select **DevKit-E8@OSPI_Release**
    again. Run **CMSIS: Build Solution** to restore the normal application run
    file, then **CMSIS: Load & Run Application**. Its startup enables XIP before
    the model is read. Check `model_verified == 1` and the application's
    `Ethos gain self-test: PASS` output on UART4, followed by advancing
    `audio_blocks` and `frames_presented` with `pipeline_error == 0`.

Whenever the compiled PTE changes, repeat the AI-layer generation, application
build, model-only file preparation, board preparation and programming steps.


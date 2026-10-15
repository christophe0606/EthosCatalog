# EthosCatalog

CMSIS solution for the Alif DevKit-E8 M55 HP core and Ethos-U85 (256 MACs).
`projects/template_mram` is a small hello world: an 8x8 NHWC image passes through
a deterministic 1x1 convolution and ReLU. Firmware compares the result with
the original PyTorch model and prints `Test_result: PASS` on UART4.
`projects/template_ospi` uses the same network and fixed test data, stores the
model separately in OSPI, and includes the preparation firmware needed before
programming it. Its normal application stages the model in SRAM0, verifies its
CRC32, then performs one checked inference. It has no inference benchmark loop
or recorded OSPI benchmark results. Firmware remains in MRAM in both templates.

| VS Code target set | Project/build context | Purpose |
| --- | --- | --- |
| `DevKit-E8@Debug` | `template_mram.Debug` | Embedded MRAM model |
| `DevKit-E8@Release` | `template_mram.Release` | Embedded MRAM model, optimized |
| `DevKit-E8@OSPI_Debug` | `template_ospi.Debug` | OSPI model, debug application |
| `DevKit-E8@OSPI_Release` | `template_ospi.Release` | OSPI model, optimized application |
| `DevKit-E8@ProgramModel` | `template_ospi.ProgramModel` | Prepare OSPI for programming; no model or inference runtime |

See the [MRAM template](projects/template_mram/README.md) and
[OSPI template](projects/template_ospi/README.md) for their build configurations.
The OSPI template has been generated and built only; it has not been loaded,
run, programmed or benchmarked as part of this change.

## Third-party submodules

`thirdparty/c_mcp` and `thirdparty/CMSIS-Stream` track the upstream repositories
for future CMSIS Pack integration. Initialize them after cloning this repository:

```sh
git submodule update --init --recursive
```

## Select a project and prepare its network

Model discovery starts with the selected project and its `model/network.yml`,
which identifies the model source, quantizer and cached model files in that
project's `model/` folder. No solution-wide model name is needed.
`mlops.model.clayer` identifies the shared generated AI layer; `create_ai_layer.py`
reads the NPU configuration from the csolution's `mlops.npu` node and model/Vela
settings from the selected project. It does not read `EthosCatalog.cbuild-mlops.yml`.
The optional legacy `.cbuild-mlops.yml` command-line argument only locates the
matching csolution. Without VS Code selection settings, the scripts can use the
contexts in `.cbuild-set.yml`; `--project <name-or-cproject-path>` selects a project
explicitly. The saved VS Code selection takes priority because generated build
files may still describe the previously selected project.

1. List applications in `EthosCatalog.csolution.yml` → `projects`, and select
   exactly one application for build in the VS Code CMSIS UI target set.
   The MRAM template is selected initially. The Python scripts follow the persisted
   target-set selection in `.vscode/cmsis.json` and its `project-context` entries;
   other projects can remain listed in the solution.
   Each project owns a `model/` directory containing the model, quantization
   script, `network.yml`, quantized caches and fixed test data, plus its application
   and linker script.
2. Install the CMSIS packs and tools selected by the solution using the CMSIS
   Solution extension and Arm Environment Manager. The required Python exporter
   and C++ runtime both use ExecuTorch 1.4.1.
3. From the solution root, create Python 3.13 and install the packages with uv:

   ```powershell
   uv venv --python 3.13
   uv run setup_venv.py --uv --python 3.13
   ```

   `setup_venv.py` uses `uv pip install` for `requirements.txt`, then installs
   `requirements-arm-tosa.txt` with `--no-deps` and verifies the Arm quantizer
   import. `setup_venv.ps1` delegates to this same script; on Linux/macOS use
   `./setup_venv.sh --uv --python 3.13`.
4. Review the selected project's `model/network.yml`. Paths in this YAML remain
   relative to the project directory. `quantize` names its project-owned
   Python quantization script. `flash_ospi` selects separate
   OSPI programming. `vela.ini`, `vela.system_config` and `vela.memory_mode`
   select the compiler memory configuration; these are project settings for
   now, rather than automatic csolution context overrides.
5. Generate the root AI layer:

   ```powershell
   uv run scripts/create_ai_layer.py
   ```

   On the first run, `create_ai_layer.py` calls the script named by `quantize`
   with `--solution <selected-solution> --project <selected-cproject>`. For the
   MRAM template this is `projects/template_mram/model/quantize.py`; it owns model loading, representative
   calibration inputs, preprocessing and quantization policy. It saves quantized
   weights and parameters in the project's `.safetensors` **before** Vela runs.
   The companion `.pt2` stores the quantized graph, because safetensors alone
   contains no network operations. Keep both files together.
   If the safetensors already exists, the exporter loads it and its graph and
   runs the delegate/Vela compilation without calling the project quantizer or
   importing the original model. To call that project script again and replace
   its cache after changing the model, calibration data or quantization policy:

   ```powershell
   uv run scripts/create_ai_layer.py --quantize
   ```

   `ai_layer/` contains the selected project's compiled `model.pte`, its C
   interface, runtime/operator layer and `generation.json`. Regenerate
   it whenever switching projects or changing Vela/storage settings. There is
   only one generated AI layer in this solution.
   Firmware test inputs, expected outputs and tolerances are maintained separately
   by each project. The template keeps its fixed `model/model_test_data.h` with
   its model files. Neither layer generation nor quantization writes that header;
   review and update it manually when the template's behavior changes.
6. Open `EthosCatalog.csolution.yml` in the CMSIS Solution extension, select
   **DevKit-E8@Debug** or **DevKit-E8@Release** for MRAM, or
   **DevKit-E8@OSPI_Debug** or **DevKit-E8@OSPI_Release** for OSPI, and **Build**. When using the
   CMSIS Developer Assistant, use `cmsis_action` with `action: build`.
   Release uses `-O3 -ffast-math` for C and C++; Debug keeps optimization off.

## Manually load and run the template (MRAM)

1. Connect the board's power, debug probe and UART connection. Install the Alif
   M55_HP debug stub/ATOC if required by your board; see
   [the board setup](board/DevKit-E8/README.md).
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
   115200 baud, 8 data bits, no parity, 1 stop bit. Expect 128 output elements,
   the maximum absolute error and `Test_result: PASS`. `app_result` holds the
   application status for debugger inspection.

## Manually program a future OSPI network

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
`projects/template_ospi/ospi_startup.c`, adapted from `M55_HP/main.c` on the
reference `Blinky_M55_HP` repository's `chess` branch. Its `app_prepare()` hook
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
`app_error == -305` means XIP is still enabled from a previous application;
read the [OSPI template's cold-start instructions](projects/template_ospi/README.md)
before retrying. The normal **OSPI_Debug/OSPI_Release** builds instead initialize
XIP and continue into the application. Both modes belong to the same project;
no separate preparation-firmware project is needed.

The existing `model_storage_prepare_programming()` helper in
`board/DevKit-E8/model_storage.c` configures pins and resets the external chip;
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
   code and wait point are what this README calls the "programming handoff";
   the OSPI template implements them in its `ProgramModel` configuration. The
   generated file uses attach with pre/post reset disabled to preserve that
   preparation.
4. Use the CMSIS Developer Assistant `flash` tool on the generated model-only
   run file. Then load and run the application's normal image with CMSIS.
   Configure OSPI XIP before inference. Repeat separate network programming
   whenever the compiled PTE changes.

The source flash loader is kept as a separate build-only solution; see
[its instructions](board/flash/loader/README.md). Hardware execution and OSPI
programming must be checked after a board is connected.

**Hands-on sequence**

This sequence uses the supplied `template_ospi`. For a derived OSPI project,
substitute its project/context names and retain the `ProgramModel` build mode.
Complete the Python/tool setup in **Select a project and prepare its network**
first. These are instructions for future hardware use; this template has only
been generated and built during its creation.

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
   configurations enable XIP; `app_main.cpp` copies the model to SRAM0, verifies
   its generated CRC32 and runs one inference.
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
    setup in [the board instructions](board/DevKit-E8/README.md) if needed.
17. Run **CMSIS: Load & Debug Application** from the Command Palette. Set a
    breakpoint at `model_programming_wait`, then continue to that breakpoint.
    Confirm `model_programming_ready == 1`, `app_stage == 5`, `app_error == 0`
    and `app_service_error == 0`. If `app_error == -305`, follow the OSPI
    template's cold-start instructions, then verify readiness again. The flash
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
    `Test_result: PASS` output on UART4. These are correctness checks, not an
    OSPI benchmark.

Whenever the compiled PTE changes, repeat the AI-layer generation, application
build, model-only file preparation, board preparation and programming steps.

## Add another project

Copy the MRAM or OSPI template's project layout and write a project-specific quantization
script with `--solution` and `--project` arguments. It owns how to load the model,
preprocess representative calibration data and choose quantization settings. Keep that script
and `network.yml` in the project's `model/` directory. Configure its project-relative
path under `network.quantize` (for example, `model/quantize.py`);
`create_ai_layer.py` invokes it directly.
The script must save the quantized exported graph and safetensors at the configured
paths using `scripts/quantized_model.py`'s cache format/helpers. That shared module
only provides compiler configuration and cache I/O; it has no calibration policy.
Set the project's network YAML and adapt its application, manually maintained
test data and linker script to the model's shapes, pool sizes and memory placement.
Keep model sources and quantized caches inside that project. Add it to the
solution's `projects` list, select it alone for build in the CMSIS UI target set,
and regenerate `ai_layer` before building. Other projects can remain listed.

For command-line/CI use, pass `--project <name-or-cproject-path>` to
`scripts/create_ai_layer.py`, `scripts/create_model_flash.py`, or the project
quantizer to select an application explicitly. Paths are relative to the solution
directory. Without an explicit project, scripts use the saved CMSIS UI selection,
then a legacy `.cbuild-set.yml` when present. A solution with only one listed
project also works without saved selection. Ambiguous selections are rejected.

**Hands-on sequence**

Choose `projects/template_mram` for an embedded MRAM model, or
`projects/template_ospi` for a separately programmed OSPI model with preparation
code. The commands below create `projects/my_network` alongside both templates.
Use a new directory name if `my_network` already exists. Complete the Python/tool
setup in **Select a project and prepare its network** first.

1. In VS Code, open the repository root folder. Choose **Terminal > New Terminal**
   and use a PowerShell terminal at that root.
2. Copy the template into the new project directory:

   ```powershell
   Copy-Item -LiteralPath projects/template_mram -Destination projects/my_network -Recurse
   ```

   For an OSPI project, use this command instead:

   ```powershell
   Copy-Item -LiteralPath projects/template_ospi -Destination projects/my_network -Recurse
   ```

3. Rename the copied CMSIS project file:

   ```powershell
   Rename-Item -LiteralPath projects/my_network/template_mram.cproject.yml -NewName my_network.cproject.yml
   ```

   If you copied the OSPI template, use this rename command instead:

   ```powershell
   Rename-Item -LiteralPath projects/my_network/template_ospi.cproject.yml -NewName my_network.cproject.yml
   ```

4. In Explorer, open `projects/my_network/model/model.py`. Replace the template
   model with your network and save it.
5. Open `projects/my_network/model/quantize.py`. Adapt model construction,
   representative calibration inputs, preprocessing, example shapes and
   quantization checks for that network. Keep its `--solution` and `--project`
   arguments, project-ownership check and shared cache-saving calls. Save it.
6. Open `projects/my_network/model/network.yml`. Set `network.name` to
   `MyNetwork` and check each project-relative path: `model`, `quantize`,
   `safetensors`, `graph` and `vela.ini`. Choose the Vela system configuration
   and memory mode that match your linker/runtime placement. For an initial
   MRAM application, keep `flash_ospi: false`. For OSPI, retain `flash_ospi: true`
   and its reserved partition. Save the file.
7. Open `projects/my_network/model/model_test_data.h`. Replace the fixed inputs,
   expected outputs and tolerance with values for your network. Save the file;
   the Python scripts will not generate this header for you.
8. Open `projects/my_network/app_main.cpp`. Adapt input shapes, buffers, output
   checks and application behavior to your network. Save the file.
9. Open `projects/my_network/linker_ac6.sct.src` and
   `projects/my_network/my_network.cproject.yml`. Adapt memory placement, pool
   sizes, required components and source-file lists. Update the copied
   `Template` group/connection descriptions as appropriate, while keeping the
   Board and AI layer references. Save both files. If you maintain
   `model/benchmark.yml`, also change its context to
   `my_network.Release+DevKit-E8` and update its model-specific settings.
10. Open `EthosCatalog.csolution.yml`. Under its existing `solution.projects`
    list, add the new entry while keeping both template entries:

    ```yaml
    projects:
      - project: projects/template_mram/template_mram.cproject.yml
        for-context: [.Debug, .Release]
      - project: projects/template_ospi/template_ospi.cproject.yml
      - project: projects/my_network/my_network.cproject.yml
    ```

    Keep this list indented under `solution`, then save the file.
11. In Explorer, right-click `EthosCatalog.csolution.yml` and choose
    **CMSIS: Select as Active Solution**.
12. Run **CMSIS: Manage Solution Settings** from the Command Palette
    (**Ctrl+Shift+P**). Select **DevKit-E8@Release**, select only `my_network` for
    build, and use its `Release` build type. Save/apply the selection. The active
    target set should now contain `project-context: my_network.Release`;
    both templates stay in the solution's project list. For an OSPI-derived
    project, also select it in the **ProgramModel** target set using its
    `ProgramModel` build type; keep its `ospi_startup.c`, OSPI component, source
    files, SRAM staging section and context filters.
13. In the repository-root terminal, force a first quantization for the new
    project so copied template caches are replaced:

    ```powershell
    uv run scripts/create_ai_layer.py --quantize
    ```

    The VS Code alternative is **Terminal > Run Task > Model: Requantize and
    create AI layer**. Confirm that `[ai_layer] selected project:` names
    `my_network.cproject.yml`, and that `ai_layer/generation.json` records
    `projects/my_network/my_network.cproject.yml`.
14. Run **CMSIS: Build Solution** from the Command Palette. Wait for a successful
    build of `my_network.Release+DevKit-E8`.
15. For an MRAM project, follow **Manually load and run the template (MRAM)**
    using your new application's target and expected results. For an OSPI
    project, complete **Manually program a future OSPI network** above before
    running inference.
16. After changing the model, calibration data or quantization policy, select
    this project in the CMSIS UI and run:

    ```powershell
    uv run scripts/create_ai_layer.py --quantize
    ```

    Then build again. When switching to another project without changing its
    quantized model, select that project in the CMSIS UI and run
    `uv run scripts/create_ai_layer.py` before building, so the shared AI layer
    belongs to the newly selected application.

Script regression checks: `uv run tests/test_project_scripts.py`.

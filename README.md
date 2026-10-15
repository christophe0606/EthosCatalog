# EthosCatalog

CMSIS solution for the Alif DevKit-E8 M55 HP core and Ethos-U85 (256 MACs).
Both templates capture the DevKit-E8 microphone at 48 kHz, apply a signed 2x
sample gain with an Ethos-U85 network, and display the animated spectrum on the
standard 480 x 800 MIPI LCD. The MRAM template embeds the model; the OSPI
template reads it directly from separately programmed OSPI flash. Firmware
remains in MRAM in both templates.

The real-time audio thread processes 512-sample blocks through independent
PCM conversion, Ethos gain, sliding buffer, DC removal, Hann window, FFT and
magnitude stages. The LCD thread runs at CMSIS High priority, below Realtime,
and receives complete spectrum snapshots without blocking audio on drawing or
VSYNC. It retains the Spectrogram example's log/linear touch toggle, display
scale slider and animated peak markers. The network gain is fixed at 2x; the
slider changes display scale only.

Reusable processing, microphone and LCD setup live under
[common/nodes_impl](common/nodes_impl/README.md). Model execution, thread
orchestration and drawing stay in each project. The project supplies the
persistent ExecuTorch pools and their memory sizes. The common initializer
constructs one EmbeddedModule that lives until reset.

| VS Code target set | Project/build context | Purpose |
| --- | --- | --- |
| `DevKit-E8@Debug` | `template_mram.Debug` | Embedded MRAM model |
| `DevKit-E8@Release` | `template_mram.Release` | Embedded MRAM model, optimized |
| `DevKit-E8@OSPI_Debug` | `template_ospi.Debug` | OSPI model, debug application |
| `DevKit-E8@OSPI_Release` | `template_ospi.Release` | OSPI model, optimized application |
| `DevKit-E8@ProgramModel` | `template_ospi.ProgramModel` | Prepare OSPI for programming; no model or inference runtime |

See the [MRAM template](projects/template_mram/README.md) and
[OSPI template](projects/template_ospi/README.md) for their build configurations.

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
   script, `network.yml`, quantized caches and gain self-test constants, plus its application
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

## Flash and run

Flashing and hardware preparation depend on the board. Follow the procedure for
your target:

- [Alif DevKit-E8: MRAM application loading and OSPI model programming](documentation/alif-e8-flashing.md)

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
gain self-test constants and linker script to the model's shapes, pool sizes and memory placement.
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
    files, SRAM inference-pool section and context filters.
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
15. Follow the [flashing and running instructions for your board](#flash-and-run)
    using your new application's target and expected results. For an OSPI
    project, complete separate model programming before running inference.
16. After changing the model, calibration data or quantization policy, select
    this project in the CMSIS UI and run:

    ```powershell
    uv run scripts/create_ai_layer.py --quantize
    ```

    Then build again. When switching to another project without changing its
    quantized model, select that project in the CMSIS UI and run
    `uv run scripts/create_ai_layer.py` before building, so the shared AI layer
    belongs to the newly selected application.

## Benchmark records

Both templates record hardware measurements and Vela estimated bandwidth in
separate `measurements` and `estimates` sections of `model/benchmark.yml`.
Memory measurements cover the model, Ethos regions and ExecuTorch pool peaks.
Each project keeps only the latest benchmark and its source data, updated in
place; Git provides the history.
`benchmark.platform` identifies the OS/RTOS and CPU
independently of CMSIS targets, with optional OS version. The shared
[benchmark schema](schema/README.md) also supports platforms such as Linux on
Cortex-A without CMSIS or Vela metadata.

Validate all benchmark YAML files with `uv run scripts/validate_benchmarks.py`.
Schema regression checks: `uv run tests/test_benchmark_schema.py`.

Script regression checks: `uv run tests/test_project_scripts.py`.
DSP and renderer checks on Windows with clang and installed CMSIS packs:
`./tests/run_audio_tests.ps1 -PackRoot C:/ctools_pack`.
The latter also generates `out/audio-spectrum-{linear,log}.ppm` previews.
See [validation notes](documentation/audio-spectrum-validation.md) for measured
board results and the processing/thread contracts.

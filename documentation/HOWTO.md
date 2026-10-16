# Build and use Ethos Catalog

This guide covers setup, model preparation, building and adding projects. Browse
available demos and their benchmarks in the [project catalog](../README.md).
The current CMSIS solution targets the Alif DevKit-E8 Cortex-M55 HP core and
Ethos-U85 NPU with 256 MACs.

Run terminal commands from the repository root unless stated otherwise. File
paths in this guide are also relative to that root.

- [Set up the tools](#set-up-the-tools)
- [Select a project and prepare its network](#select-a-project-and-prepare-its-network)
- [Flash and run](#flash-and-run)
- [Add another project](#add-another-project)
- [Project catalog](#project-catalog)
- [Benchmark records](#benchmark-records)
- [Validation checks](#validation-checks)

Changed projects are checked automatically on pushes and pull requests. See
[GitHub CI validation](ci.md) for build order, required project files and
downloadable error reports.

## Set up the tools

1. Open the repository root in VS Code. Install the CMSIS Solution extension,
   Arm Environment Manager and uv. Use the extensions to install the packs and
   tools specified by `EthosCatalog.csolution.yml`.
2. Initialize the third-party submodules:

   ```sh
   git submodule update --init --recursive
   ```

   `thirdparty/c_mcp` and `thirdparty/CMSIS-Stream` track their upstream
   repositories. The solution uses the local CMSIS Stream pack through its
   `path` entry.
3. Create a Python 3.13 environment and install its dependencies:

   ```powershell
   uv venv --python 3.13
   uv run setup_venv.py --uv --python 3.13
   ```

   The Python exporter and C++ runtime must use matching ExecuTorch versions;
   this solution specifies 1.4.1. The setup script installs `requirements.txt`,
   installs `requirements-arm-tosa.txt` with `--no-deps`, and checks the Arm
   quantizer import. `setup_venv.ps1` runs the same script; on Linux or macOS,
   you can use `./setup_venv.sh --uv --python 3.13`.

## Select a project and prepare its network

### Choose the application

Open `EthosCatalog.csolution.yml` and select a target set in the CMSIS Solution
extension. Select exactly one application for building; other applications can
remain in the solution's `projects` list.

| VS Code target set | Project/build context | Purpose |
| --- | --- | --- |
| `DevKit-E8@Debug` | `template_mram.Debug` | Microphone spectrum demo with an embedded MRAM model |
| `DevKit-E8@Release` | `template_mram.Release` | MRAM demo with optimization enabled |
| `DevKit-E8@OSPI_Debug` | `template_ospi.Debug` | Microphone spectrum demo with a model in OSPI flash |
| `DevKit-E8@OSPI_Release` | `template_ospi.Release` | OSPI demo with optimization enabled |
| `DevKit-E8@ProgramModel` | `template_ospi.ProgramModel` | Prepare OSPI for programming, without inference |
| `DevKit-E8@KWS_Debug` | `kws_micronet_l.Debug` | Live MicroNet Large keyword-spotting demo |
| `DevKit-E8@KWS_Release` | `kws_micronet_l.Release` | Keyword-spotting demo with optimization enabled |

See the [MRAM template](../projects/template_mram/README.md),
[OSPI template](../projects/template_ospi/README.md) and
[keyword-spotting project](../projects/kws_micronet_l/README.md) for application
behavior and validation details.

The Python scripts follow the saved target-set selection in `.vscode/cmsis.json`.
For command-line or CI use, select a project explicitly:

```powershell
uv run scripts/create_ai_layer.py --project template_mram
```

`--project` accepts a project name or a cproject path relative to the solution
directory. It is also supported by `scripts/create_model_flash.py` and the project
quantizers. Without an explicit project, scripts use the saved CMSIS selection,
then a legacy `.cbuild-set.yml` if no saved selection exists. A solution with only
one listed project also works without a selection. Ambiguous selections are
rejected.

### Review the network settings

Each project owns a `model/network.yml`. Its file paths are relative to the
project directory, one level above `model/`.

| Setting | Purpose |
| --- | --- |
| `model` | Model source |
| `quantize` | Project-specific quantization or import script |
| `safetensors` and `graph` | Cached quantized tensors and companion `.pt2` graph |
| `flash_ospi` | Whether to program the model separately in OSPI flash |
| `vela.ini`, `vela.system_config`, `vela.memory_mode` | Vela compiler and memory configuration |

OSPI projects also define `ospi.address` and `ospi.size` for their reserved model
partition. Vela settings come from the project configuration; changing a CMSIS
build context does not automatically override them.

Check the configuration against the shared [network schema](../schema/README.md#network-configuration):

```powershell
uv run scripts/validate_networks.py projects/kws_micronet_l/model/network.yml
```

Omit the file argument to validate every project's network configuration. The
command exits nonzero if any file fails, so it can also be used in CI.

### Generate the AI layer

```powershell
uv run scripts/create_ai_layer.py
```

On the first run, the script calls the selected project's `quantize` script with
`--solution` and `--project`. That script owns model loading, calibration,
preprocessing and quantization or import policy. It saves quantized tensors in
`.safetensors` and the exported graph in `.pt2` before Vela compilation. Keep
both cache files together: tensors alone do not describe the network operations.

When a cache exists, the exporter reuses it without importing the original model
or calling the project quantizer. To replace it after changing the model,
calibration data or quantization policy, run:

```powershell
uv run scripts/create_ai_layer.py --quantize
```

The generated `ai_layer/` contains `model.pte`, its C interface, the runtime and
operator layer, and `generation.json`. There is one shared AI layer per solution.
Regenerate it whenever you switch projects or change Vela or storage settings.
Check the selected project in the script output and in `ai_layer/generation.json`
before building.

The generator reads the NPU configuration from the solution's `mlops.npu` node
and the model settings from the selected project. `mlops.model.clayer` identifies
the generated layer. A legacy `.cbuild-mlops.yml` argument only locates the
matching solution; its contents are not used.

### Generate the Stream graph and build

Run the selected project's `app.py`, for example:

```powershell
uv run projects/template_mram/app.py
```

Use `projects/template_ospi/app.py` or `projects/kws_micronet_l/app.py` for the
other applications. You can also run `uv run app.py` from the project folder.
Generated C++ and configuration files are checked in; regenerate them after
changing the graph. Graphviz `dot` is optional. If PDF conversion is unavailable
or fails, the generated code and DOT diagram remain available.

Build the selected target with **CMSIS: Build Solution**. When using the CMSIS
Developer Assistant, use `cmsis_action` with `action: build`. Release uses
`-O3 -ffast-math`; Debug disables optimization.

## Flash and run

Follow the [Alif DevKit-E8 flashing guide](alif-e8-flashing.md) for board setup,
MRAM application loading and separate OSPI model programming.

Both spectrum templates keep firmware in MRAM. The MRAM template embeds its
model; the OSPI template reads its model directly from separately programmed
OSPI flash. Complete model programming before running an OSPI application, then
check the startup self-test and application output as described in its README.

## Add another project

Complete the [tool setup](#set-up-the-tools) first. The example below creates
`projects/my_network`; choose a different name if that directory already exists.

1. Copy the template that matches your model storage:

   ```powershell
   Copy-Item -LiteralPath projects/template_mram -Destination projects/my_network -Recurse
   Rename-Item -LiteralPath projects/my_network/template_mram.cproject.yml -NewName my_network.cproject.yml
   ```

   For a separately programmed OSPI model, use these commands instead:

   ```powershell
   Copy-Item -LiteralPath projects/template_ospi -Destination projects/my_network -Recurse
   Rename-Item -LiteralPath projects/my_network/template_ospi.cproject.yml -NewName my_network.cproject.yml
   ```

2. Replace `model/model.py` in the new project with your network. Adapt
   `model/quantize.py` to its model construction, representative inputs,
   preprocessing and quantization or import policy. Keep the `--solution` and
   `--project` arguments, project-ownership checks and shared cache-saving calls.
   Use the cache format and helpers in `scripts/quantized_model.py` to save the
   graph and tensors. That module provides cache I/O and compiler configuration;
   calibration policy belongs to the project.
3. Edit `projects/my_network/model/network.yml`. Set `network.name` and review
   the project-relative `model`, `quantize`, `safetensors`, `graph` and `vela.ini`
   paths. Choose Vela settings that match the model's memory placement. Keep
   model sources and caches inside the project. For MRAM, use `flash_ospi: false`;
   for OSPI, retain `flash_ospi: true` and a reserved `ospi` partition.
4. Adapt `app_main.cpp`, the nodes in `nodes/` and `nodes_impl/`, and the graph in
   `app.py` to your application. Change its imports to `projects.my_network.nodes`
   and update project-specific node `folder` properties. Update
   `model/model_test_data.h` with suitable test inputs, expected outputs and
   tolerances; quantization and AI layer generation do not write this header.
5. Update `linker_ac6.sct.src` and `my_network.cproject.yml` for the model's shapes,
   pool sizes, memory placement, components and source files. Rename copied
   template groups and connections as appropriate, keeping the Board and AI
   layer references. For OSPI, retain `ospi_startup.c`, the OSPI component and
   helper sources, the SRAM inference-pool section and the context filters.
6. Add the project to `solution.projects` in `EthosCatalog.csolution.yml`, keeping
   the existing entries:

   ```yaml
   - project: projects/my_network/my_network.cproject.yml
   ```

   For an MRAM project, limit this entry to `.Debug` and `.Release`, as the MRAM
   template does. For OSPI, retain the `ProgramModel` build mode.
7. In VS Code, right-click `EthosCatalog.csolution.yml` and choose
   **CMSIS: Select as Active Solution**. Run **CMSIS: Manage Solution Settings**,
   choose **DevKit-E8@Release**, and select only `my_network` with the `Release`
   build type. The saved target set should contain
   `project-context: my_network.Release`. For OSPI, also select the new project
   in the **ProgramModel** target set with the `ProgramModel` build type.
8. Replace the copied quantized caches and generate the new graph:

   ```powershell
   uv run scripts/create_ai_layer.py --project my_network --quantize
   uv run projects/my_network/app.py
   ```

   Confirm that the AI layer output and `ai_layer/generation.json` identify
   `projects/my_network/my_network.cproject.yml`. The VS Code task
   **Model: Requantize and create AI layer** is an alternative to the first
   command and uses the saved CMSIS selection.
9. Run **CMSIS: Build Solution** and check that
   `my_network.Release+DevKit-E8` builds successfully. Follow the
   [flashing instructions](#flash-and-run) and verify your application's expected
   results. For OSPI, program the model separately before running inference.
10. Update the project's README metadata and
    [regenerate the catalog](#project-catalog). If you retain a copied
    `model/benchmark.yml`, replace the template results with measurements for
    your application and update its context to `my_network.Release+DevKit-E8`.

After changing the model or quantization policy, regenerate the AI layer with
`--quantize` and rebuild. When switching projects without changing the model,
regenerate it without `--quantize`. The shared AI layer must always belong to the
application being built.

Reusable Python node descriptions live in `common/nodes/`; implementations live
in `common/nodes_impl/` under the appropriate backend. Keep application-specific
nodes in the project's `nodes/` and `nodes_impl/` folders, and board interfaces
in `board/<board>/`. See [shared implementations](../common/nodes_impl/README.md)
and [Stream runtime configuration](../common/stream/README.md) for details.

## Project catalog

Each `projects/<project>/README.md` must begin with YAML metadata:

```yaml
---
description: A one-line description of the demo.
category: audio
---
```

Categories are free-form labels such as `audio`, `image`, `video`, `speech`,
`music` or `text`. The generator converts them to lowercase and sorts categories
and project names alphabetically. Each table links to the project README and
its `model/benchmark.yml`, or shows a dash if no benchmark exists.

Edit `documentation/readme_header.md` to change the README introduction. After
editing that header, adding a project, changing its metadata or adding a
benchmark file, regenerate the root README:

```powershell
uv run scripts/update_catalog.py
```

The generator checks project metadata before writing the README. It places the
header first, followed by the generated catalog.

## Benchmark records

Store each project's latest results for its different configurations in
`model/benchmark.yml`, along with the matching source data. The `benchmarks`
mapping contains shared `network` details and a `benchmark` list with one entry
per configuration. Update a configuration's entry in place; Git provides the
history. Each entry references its own UART capture and Vela summary when present.

Keep hardware measurements in `measurements` and compiler estimates, such as
Vela bandwidth estimates, in `estimates`. Memory measurements cover model
storage, Ethos regions and ExecuTorch pool peaks. Each entry's `platform` identifies
the OS or RTOS and CPU, with an optional OS version. The
[benchmark schema](../schema/README.md) also supports platforms such as Linux on
Cortex-A without CMSIS or Vela metadata.

## Validation checks

Run the checks relevant to your changes from the repository root:

| Check | Command |
| --- | --- |
| Network configuration files | `uv run scripts/validate_networks.py` |
| Network schema regression tests | `uv run tests/test_network_schema.py` |
| Benchmark files | `uv run scripts/validate_benchmarks.py` |
| Benchmark schema regression tests | `uv run tests/test_benchmark_schema.py` |
| Project script regression tests | `uv run tests/test_project_scripts.py` |
| DSP and renderer checks on Windows | `./tests/run_audio_tests.ps1 -PackRoot C:/ctools_pack` |

The DSP and renderer checks require clang and installed CMSIS packs; adjust
`-PackRoot` to your installation. They also generate
`out/audio-spectrum-{linear,log}.ppm` previews. See the
[validation notes](audio-spectrum-validation.md) for board results and audio/LCD
processing contracts. Verify firmware changes on the board as well as on the host.

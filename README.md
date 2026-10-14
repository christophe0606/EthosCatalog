# EthosCatalog

CMSIS solution for the Alif DevKit-E8 M55 HP core and Ethos-U85 (256 MACs).
`projects/template` is a small hello world: an 8x8 NHWC image passes through
a deterministic 1x1 convolution and ReLU. Firmware compares the result with
the original PyTorch model and prints `Test_result: PASS` on UART4.

## Select a project and prepare its network

1. Keep exactly one application in `EthosCatalog.csolution.yml` → `projects`.
   The template is selected initially. When changing projects, also change the
   target-set `project-context` entries to that project's Debug/Release contexts.
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
   with `--solution <selected-solution>`. For the template this is
   `projects/template/model/quantize.py`; it owns model loading, representative
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
   **DevKit-E8@Debug** or **DevKit-E8@Release**, and **Build**. When using the
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
pin/reset/XIP setup; the template does not link them.

1. Generate the selected project's AI layer and build its firmware with CMSIS.
2. Prepare a **model-only** programming description from its application run
   file (use the path CMSIS generated for your selected target):

   ```powershell
   uv run scripts/create_model_flash.py --run out/EthosCatalog+DevKit-E8.cbuild-run.yml --algorithm board/flash/DevKit-E8_IS25WX.FLM
   ```

   The output defaults to `out/<project>-model.cbuild-run.yml`. The script
   reads the selected project and checks its partition, generation hashes and
   programming algorithm. It never connects to the board. For the template it
   prints that MRAM application loading is sufficient and writes no flash file.
3. Establish the selected project's programming handoff: power/clock and
   initialize the OSPI pins/reset, leave XIP disabled, halt the core, disable
   cache/MPU and clear stack limits before the RAM flash algorithm runs. This
   is project/board-specific firmware; the template has no OSPI programming
   handoff. The generated file uses attach with pre/post reset disabled to
   preserve that preparation.
4. Use the CMSIS Developer Assistant `flash` tool on the generated model-only
   run file. Then load and run the application's normal image with CMSIS.
   Configure OSPI XIP before inference. Repeat separate network programming
   whenever the compiled PTE changes.

The source flash loader is kept as a separate build-only solution; see
[its instructions](board/flash/loader/README.md). Hardware execution and OSPI
programming must be checked after a board is connected.

## Add another project

Copy the template's project layout and write a project-specific quantization
script with a `--solution` argument. It owns how to load the model, preprocess
representative calibration data and choose quantization settings. Keep that script
and `network.yml` in the project's `model/` directory. Configure its project-relative
path under `network.quantize` (for example, `model/quantize.py`);
`create_ai_layer.py` invokes it directly.
The script must save the quantized exported graph and safetensors at the configured
paths using `scripts/quantized_model.py`'s cache format/helpers. That shared module
only provides compiler configuration and cache I/O; it has no calibration policy.
Set the project's network YAML and adapt its application, manually maintained
test data and linker script to the model's shapes, pool sizes and memory placement.
Keep model sources and quantized caches inside that project. Select it alone in
the csolution and regenerate `ai_layer` before building.

Script regression checks: `uv run tests/test_project_scripts.py`.

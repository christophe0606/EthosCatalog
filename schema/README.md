# YAML schemas

## Network configuration

[network.schema.yml](network.schema.yml) is a JSON Schema (Draft 2020-12),
written in YAML, for project `model/network.yml` files. After installing the root
`requirements.txt`, check one file or all project network files:

```powershell
uv run scripts/validate_networks.py projects/kws_micronet_l/model/network.yml
uv run scripts/validate_networks.py
```

Explicit file arguments are relative to the current working directory; multiple
files and files outside the repository are accepted. Without arguments, the
validator scans `projects/` in this repository for `network.yml` and
`network.yaml`, regardless of the current working directory. It prints `OK` for
valid files and reports errors with the file and YAML property path to stderr.
It exits with status 0 when every file passes, 1 for invalid or unreadable files,
and 2 for command usage errors or an empty scan.

The `network` mapping requires `name`, `model`, `quantize`, `quantization`,
`safetensors`, `graph`, `flash_ospi` and `vela`. Configuration text fields must
contain a non-whitespace character. `vela` requires `ini`, `system_config` and
`memory_mode`. Unknown configuration keys are rejected outside the unrestricted
`quantization.error` value. Duplicate and non-string YAML mapping keys are rejected.

`network.quantization` records how the network was quantized. All three fields
are required:

```yaml
quantization:
  type: post_training_quantization
  quantization: synthetic
  # No float comparison has been recorded yet.
  error: null
```

- `type`: `quantization_aware_training` or `post_training_quantization`. Current
  catalog projects use only post-training quantization.
- `quantization`: `full` for the complete original-source test patterns,
  `partial` for a subset of those patterns, `synthetic` for generated calibration
  data, or `already` when importing an already quantized network. `already`
  describes the imported model, regardless of the inputs used to validate it.
- `error`: network-dependent error or quality measurements. The field is required,
  but its value has no prescribed type, structure, metric names, units or ranges.
  Scalars, text, lists, matrices, nested mappings and multiple metrics are accepted;
  use `null` when no measurement is available. Describe each metric, its units,
  reference and evaluation inputs in the value or YAML comments. A tolerance is not
  a measured error. **For an already quantized network, give a comparison against
  float only if the original source supplied it.** Comparisons against another
  quantized model do not qualify as comparisons against float.

For example, a project may record recognition percentages, a confusion matrix,
or perplexity. These illustrative keys and values are not schema requirements:

```yaml
error:
  recognition_percent:
    float: 96.5
    quantized: 96.1
  confusion_matrix:
    labels: [negative, positive]
    quantized: [[92, 8], [5, 95]]
  perplexity:
    float: 12.4
    quantized: 12.7
```

Both templates use synthetic calibration inputs. KWS imports an already
quantized TFLite network and has no original-source float error to report.
The templates name their existing measurement `error.max_absolute_error`; this
name is specific to those projects, not enforced by the schema.

`flash_ospi` must be a boolean. When true, `ospi` is required with a nonnegative
integer `address` and a positive integer `size`, both in bytes. Hexadecimal YAML
integers such as `0xC0000000` are accepted; quoted numbers and booleans are not
integers. `ospi.pname` is optional (the flash script defaults to `M55_HP`). An
`ospi` mapping may be retained when `flash_ospi` is false, but must still be valid.

Paths are interpreted relative to the project directory, one level above
`model/`. Model sources, quantizers and caches belong inside the project;
`vela.ini` may reference shared configuration outside it. Schema validation
checks structure and types without loading models or requiring generated cache
files. Python readers use `validate_networks.load_network()` to enforce the same
schema, including quantization metadata. The project selection code additionally
checks resolved path containment and the presence of the Vela ini file when
preparing a network.

VS Code associates the schema with project `network.yml` and `network.yaml`
files through `.vscode/settings.json` when YAML language support is installed.
Extend the shared schema when adding configuration fields. Regression checks:

```powershell
uv run tests/test_network_schema.py
```

## Benchmark YAML

[benchmark.schema.yml](benchmark.schema.yml) is a JSON Schema (Draft 2020-12),
written in YAML. All projects use this structure. Objects reject unknown keys;
required fields and numeric types catch missing data, typos and inconsistent units.
VS Code associates it with `projects/**/*benchmark*.yml` and `*.yaml` through
`.vscode/settings.json` when YAML language support is installed.

After installing the root `requirements.txt`, validate every benchmark from the
repository root:

```powershell
uv run scripts/validate_benchmarks.py
```

The command scans all project benchmark YAML files, including both `.yml` and
`.yaml`, and exits nonzero on validation errors.
To check specific files (including files outside `projects/`):

```powershell
uv run scripts/validate_benchmarks.py projects/template_mram/model/benchmark.yml projects/template_ospi/model/benchmark.yml
```

The validator also rejects duplicate mapping keys, non-string keys and non-finite
numbers. Timestamps must be quoted RFC 3339 strings with a timezone.

Each project's `model/benchmark.yml` contains a `benchmarks` mapping with one
shared `network` and a nonempty `benchmark` list. Every list entry describes one
configuration with its own `platform`, `measurements`, `run` and optional
`estimates`. Network properties are required under `benchmarks.network` and are
not accepted inside individual entries. The old top-level `benchmark` layout is
no longer accepted.

Keep the latest result for each configuration in the same file. Different
configurations can vary the CPU, OS, clocks, memory placement or compiler
settings; describe those differences in each entry's `platform` and `run`.
Keep UART captures and Vela summaries alongside the YAML and reference the
matching files from each entry. Use distinct evidence filenames when recording
different configurations. Update a configuration's result in place; Git
provides the history rather than separate historical benchmark snapshots.

## Platform identity

Every entry in `benchmarks.benchmark` has `platform.os` and `platform.cpu`.
`os` names either the OS or RTOS; `os_version` is optional. CPU and OS names are
free text, independent of CMSIS targets, pack names or Vela configuration. For
example, the templates use:

```yaml
platform:
  os: RTX5
  os_version: 5.9.1
  cpu: Cortex-M55
```

`run.target`, `run.context`, `run.packs`, device/core identifiers and NPU details
remain optional provenance. A future Cortex-A benchmark can omit all of them,
along with the optional compiler estimates and network-memory details:

```yaml
benchmarks:
  network:
    operation: y = 2*x
    input_shape: [1, 512]
    output_shape: [1, 512]
  benchmark:
    - platform:
        os: Linux
        cpu: Cortex-A78
      measurements:
        time:
          ms: 1.25
      run:
        recorded_at: '2026-10-15T12:00:00Z'
        samples: 10
        test_result: PASS
    - platform:
        os: RTX5
        cpu: Cortex-M55
      measurements:
        time:
          ms: 2.5
      run:
        recorded_at: '2026-10-16T12:00:00Z'
        samples: 20
        test_result: PASS
```

This illustrative example shows all mandatory fields for two configurations of
the same network. Additional measurements use the same schema
properties on every platform; optional sections are omitted when inapplicable.
Extend the shared schema when adding a new metric.

## Network memory measurements

`measurements.memory` contains the observed memory components for running the
network. Both templates report:

```yaml
memory:
  model_bytes: 3072
  input_tensor_bytes: 2048
  ethos:
    weights_region_bytes: 192
    arena_region_bytes: 1216
  executorch:
    method_pool_peak_bytes: 3752
    temp_pool_peak_bytes: 1216
```

The model and Ethos region sizes come from the runtime/driver values printed in
the UART capture. `input_tensor_bytes` is the exact storage size of the
caller-owned input tensor (512 float32 values in these templates). Pool peaks
are the actual allocator high-water marks after
model loading, warmup and inference, including allocation alignment. They are
not the application-configured pool capacities.

The weights region is part of the compiled model. The Ethos arena is allocated
from the ExecuTorch temporary pool in these templates, so its 1216 bytes are
already included in the temporary pool peak. These fields describe overlapping
components; do not add them together as a total. They do not claim to measure
untracked C++ heap usage or the total process footprint.

Physical memory types and clocks belong to `platform.memory.weights` and
`platform.memory.activations`. The schema excludes firmware/application memory
breakdowns, pool reservations, linker section names and memory addresses.

## Vela estimates and units

Each entry in `benchmarks.benchmark` holds `measurements` for observed results
and optional `estimates` for predictions. Vela configuration, provenance,
estimated cycles, time and bandwidth all live under `estimates.vela`; they are
not accepted under `measurements` or as a top-level `vela` section.

The two templates' estimates were collected with Vela 5.1.0 by
recompiling their existing quantized caches using their configured system and
memory mode. Both compiled PTE hashes were verified against `run.model_sha256`.
Within `estimates.vela`, `model_sha256` and `ini_sha256` identify the estimated
model and exact configuration. `ini` is project-relative; `summary` is relative
to the benchmark YAML and points to the checked-in, unrounded Vela summary CSV.

Each bandwidth entry records the physical memory name and its Vela CSV prefix.
The shared `sram` and `weights` entries have the same fields on both templates:

- `total_bytes_per_inference`: CSV `<area>_total_bytes / batch_size`.
- `average_bytes_per_second`: CSV `<area>_total_bytes / inference_time`.
- `design_peak_bytes_per_second`: CSV `<area>_bandwidth * 2^30`.

The CSV's `inference_time` is in seconds; `estimates.vela.time_ms` multiplies it
by 1000. `cycles` is CSV `cycles_total`. Average bandwidth uses Vela's estimated
runtime, and design peak uses the configured interface capacity. These are
configuration-based estimates. MRAM maps to Vela `off_chip_flash`; OSPI1 maps to
Vela `dram`. Vela's rounded console figures are not used for these calculations.

Measured times ending in `ms` are milliseconds, clocks ending in `hz` are Hz,
and clocks ending in `mhz` are MHz. Network model, region and pool sizes are in
bytes; cycle/tick counts are
unitless integers (aggregate cycle statistics may be fractional).

Schema and recorded-summary regression checks:

```powershell
uv run tests/test_benchmark_schema.py
```

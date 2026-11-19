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
`flash_ospi` and `vela`, plus either a `safetensors`/`graph` pair or `methods`.
Configuration text fields must
contain a non-whitespace character. `vela` requires `ini`, `system_config` and
`memory_mode`. Unknown configuration keys are rejected outside the unrestricted
`quantization.error` value. Duplicate and non-string YAML mapping keys are rejected.

The single-graph pair exports the method `forward`. A multi-method model instead
defines a nonempty `methods` mapping; it cannot also define the top-level cache
pair. Each method has `graph` and `safetensors` paths, plus optional boolean
`delegate` (default `true`). Method names must be identifiers. All methods are
compiled into one PTE. A method with `delegate: false` may run only on CPU;
every other method must produce an Ethos delegate. Flash provenance covers
every named graph and tensor cache, including CPU methods.

Optional boolean `extract_delegate_segments` defaults to `false`. Setting it to
`true` places backend payloads in separate segments within the same PTE file,
so a project DataLoader can handle them individually. It does not implement
staging or change buffer/cache policy; regenerate the AI layer when changing it.

```yaml
methods:
  backbone:
    graph: model/backbone_quantized.pt2
    safetensors: model/backbone_quantized.safetensors
  text_embedding:
    graph: model/text_embedding.pt2
    safetensors: model/text_embedding.safetensors
    delegate: false
```

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

OSPI models may declare immutable `assets`, keyed by lowercase identifiers. Each
entry has a project-relative `file` contained in `model/` and an integer `address`.
Generation checks that all asset bytes fit the model partition and overlap
neither the PTE nor another asset. Generation provenance records asset size,
SHA-256 and CRC-32; flash preparation rejects changed assets and emits one binary
output per asset. The generated header provides `MODEL_ASSET_<NAME>_ADDRESS`,
`MODEL_ASSET_<NAME>_SIZE_BYTES` and `MODEL_ASSET_<NAME>_CRC32` for firmware readback.
`assets` requires `flash_ospi: true`. Writable runtime state must be copied to RAM.

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

Use `benchmarks.networks` for new records in `model/benchmark.yml`, including
single-network projects. Each item contains `network` metadata and a nonempty
`benchmark` list. Every benchmark entry records one configuration with its own
`platform`, `measurements`, `run` and optional `estimates`.

Give each network a stable `network.name`. For a PTE method, specify both the
project-relative `network.pte` path and the exact exported `network.method`,
including `forward` for a default forward method. Store the configurations for
that method in the same item. The validator checks that names and PTE/method
pairs are unique within the file. Native and end-to-end workloads can use a
name alone. The existing `benchmarks.network` and `benchmarks.benchmark` layout
is still accepted, with identity fields optional. Use one layout per file.

This example records two methods measured independently from the same PTE.

```yaml
benchmarks:
  networks:
    - network:
        name: Encoder
        pte: ai_layer/model.pte
        method: encode
        operation: Encode an input frame
        input_shape: [1, 512]
        output_shape: [1, 128]
      benchmark:
        - platform: {os: RTX5, cpu: Cortex-M55}
          measurements:
            time: {ms: 2.5}
          run:
            recorded_at: '2026-10-16T12:00:00Z'
            samples: 20
            test_result: PASS
    - network:
        name: Decoder
        pte: ai_layer/model.pte
        method: decode
        operation: Decode the representation with a conditioning tensor
        input_shapes: [[1, 128], [1, 32]]
        output_shape: [1, 512]
      benchmark:
        - platform: {os: RTX5, cpu: Cortex-M55}
          measurements:
            time: {ms: 3.75}
          run:
            recorded_at: '2026-10-16T12:05:00Z'
            samples: 20
            test_result: PASS
```

Use `network.input_shape` or `network.output_shape` for a single tensor on that
side of the method interface. For multiple tensors, use `network.input_shapes`
or `network.output_shapes` with at least two shapes in runtime order. For example,
the face detector records `output_shapes: [[1, 6, 6, 18], [1, 12, 12, 18]]`.

Measure one method at a time with representative inputs. Record its own timings,
correctness result and available memory observations. Keep any combined timings
under a separate workload name and describe their scope in `run.timing_scope`.
Use the entry's `platform` and `run` fields to describe differences in CPU, OS,
clocks, memory placement or compiler settings. Keep matching UART captures and
Vela summaries alongside the YAML and use distinct filenames for each method and
configuration. Update each configuration in place and use Git for its history.

## Platform identity

Each benchmark entry requires `platform.os` and `platform.cpu`. Use the OS or
RTOS name and CPU model, for example `RTX5` on `Cortex-M55` or `Linux` on
`Cortex-A78`. `platform.os_version` is optional. The platform fields are
independent of CMSIS targets, pack names and Vela configuration. `run.target`,
`run.context`, `run.packs`, device/core identifiers and NPU details are optional
provenance, as are compiler estimates and network-memory measurements.
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
the UART capture. For a PTE with several methods, `model_bytes` is the full PTE
size shared by those entries. Ethos regions and allocator peaks are observations
from benchmarking the named method. `input_tensor_bytes` is the total storage
for the caller-owned input tensors (512 float32 values in these templates).
Pool peaks are allocator high-water marks after model loading, warmup and
inference, including allocation alignment.

The weights region is part of the compiled model. The Ethos arena is allocated
from the ExecuTorch temporary pool in these templates, so its 1216 bytes are
already included in the temporary pool peak. These fields describe overlapping
components; do not add them together as a total. They do not claim to measure
untracked C++ heap usage or the total process footprint.

Physical memory types and clocks belong to `platform.memory.weights` and
`platform.memory.activations`. The schema excludes firmware/application memory
breakdowns, pool reservations, linker section names and memory addresses.

## Vela estimates and units

Each benchmark entry holds `measurements` for observed results and optional
`estimates` for predictions. Vela configuration, provenance, estimated cycles,
time and bandwidth live under `estimates.vela`. Include Vela estimates for a
named method when the summary corresponds to that method.

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

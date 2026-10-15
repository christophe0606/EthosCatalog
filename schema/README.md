# Benchmark YAML

[benchmark.schema.yml](benchmark.schema.yml) is a JSON Schema (Draft 2020-12),
written in YAML. Both templates use this structure. Objects reject unknown keys;
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

Each project keeps only its latest `model/benchmark.yml`, UART capture and Vela
summary. Update these files in place; Git provides the history. Do not add
historical benchmark snapshots or separate retest records.

## Platform identity

Every benchmark has `platform.os` and `platform.cpu`. `os` names either the OS or
RTOS; `os_version` is optional. CPU and OS names are free text, independent of
CMSIS targets, pack names or Vela configuration. For example, the templates use:

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
benchmark:
  platform:
    os: Linux
    cpu: Cortex-A78
  measurements:
    time:
      ms: 1.25
  run:
    recorded_at: '2026-10-15T12:00:00Z'
    samples: 10
    test_result: PASS
  network:
    operation: y = 2*x
    input_shape: [1, 512]
    output_shape: [1, 512]
```

This shows all mandatory fields. Additional measurements use the same schema
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

One YAML file holds two distinct sections: `benchmark.measurements` for observed
results and `benchmark.estimates` for predictions. Vela configuration, provenance,
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

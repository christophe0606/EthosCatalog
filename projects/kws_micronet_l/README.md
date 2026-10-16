---
description: Prequantized MicroNet Large keyword spotting through PyTorch and ExecuTorch; validated and benchmarked on Ethos-U85 with MRAM weights.
category: audio
---

# MicroNet Large keyword spotting

This project starts from `template_mram` and adapts the network:
the original quantized TFLite is imported as a PyTorch exported program and
quantized safetensors, ready for the existing ExecuTorch Arm backend to call
Vela. No training checkpoint, calibration data, or requantization is used.

The current firmware tests and benchmarks only the network. Its project file
builds the int8 harness and embedded ExecuTorch runtime, with no microphone,
LCD or CMSIS Stream execution. The copied demo files are unused scaffold for a
later application stage. MFCC preprocessing and a speech demo remain future
work. `KWS_Debug` and `KWS_Release` target sets select this project.

## Board validation and benchmark

`DevKit-E8@KWS_Release` was built, loaded and run on the Alif E8 M55_HP with
Ethos-U85-256. Both CPU and AXI/NPU clocks were 400 MHz. The ExecuTorch Arm
backend produced one NPU delegate and zero CPU fallback operators. Weights
remain in MRAM and the NPU arena is in SRAM0.

| Measured quantity | Result |
| --- | ---: |
| NPU latency, median of 100 inferences | 2.261480 ms / 904,592 cycles |
| NPU cycle range | 904,550–904,814 |
| ExecuTorch `forward()`, median | 2.344878 ms |
| PTE size, including NPU constants | 559,680 bytes |
| NPU weights region within the PTE | 553,328 bytes |
| NPU arena / ExecuTorch temporary peak, same allocation | 371,232 bytes |
| ExecuTorch method allocator peak | 788 bytes |
| Input tensor | 490 bytes |

Timing follows 270 validation forwards and one immediate warmup on fixed
synthetic input `random_0`. The timed loop contains no UART or debugger halt.
`forward()` includes runtime and PMU hooks; input/output copies, tensor setup,
validation and reporting are outside that timer. PMCCNTR matched NPU_ACTIVE
on every inference, with one NPU invocation and no overflow. The harness
reserves 64 KiB and 512 KiB pools; the table reports observed allocation peaks,
not reserved firmware memory. Arena and temporary peak must not be added.

All 3,240 output scores across 270 locally generated int8 feature tensors were
checked on the board and independently verified from the captured UART output:
maximum error was one score step against default TFLite and PyTorch, and two
against reference TFLite. Top-1 agreement was 269/270 with default TFLite and
PyTorch, and 270/270 with reference TFLite. These synthetic tests establish
conversion and runtime consistency; they do not measure speech accuracy.

[benchmark.yml](model/benchmark.yml) records all 100 timing samples, platform,
memory and hashes; [board_validation.json](model/board_validation.json) records
the independent score checks, including the differing pattern. The raw capture
is [benchmark_uart.txt](model/benchmark_uart.txt). Vela's separate estimate is
5.884533 ms, recorded with its [raw CSV](model/benchmark_vela_summary.csv).

To reproduce, regenerate the shared AI layer as below, select
`DevKit-E8@KWS_Release`, then build and load/debug through CMSIS Developer
Assistant. Capture UART4 at 115200 8N1 before the startup test runs. The harness
prints `Test_result: PASS` and remains running with `kws_heartbeat` advancing.
`pipeline_error`, `app_result` and `npu_error` were all zero under the matching
KWS firmware symbols. Regenerate fixtures only when the model changes:

```powershell
.venv/Scripts/python.exe projects/kws_micronet_l/model/generate_board_tests.py --reference-python tmp/tflite-reference/Scripts/python.exe
.venv/Scripts/python.exe projects/kws_micronet_l/model/record_benchmark.py --build-job <build-job> --execution-job <load-job>
.venv/Scripts/python.exe scripts/validate_benchmarks.py
```

The recorder reads `model/benchmark_uart.txt`, checks all outputs and samples,
and writes `board_validation.json` and `benchmark.yml`. It requires the matching
shared AI layer and Release build artifacts, plus the original fixture hashes.

## Model interface and provenance

Source: [Arm ML-zoo MicroNet Large INT8](https://github.com/Arm-Examples/ML-zoo/tree/master/models/keyword_spotting/micronet_large/tflite_int8),
Apache-2.0. Only the model and small metadata were downloaded; no test patterns
or speech datasets were downloaded.

`model/kws_micronet_l.tflite` is 658,832 bytes, SHA256
`15723bcc7d73599546cfebd289632799393e329df84bba883205b08b666a1aa3`.
The upstream SHA1 is `5ac522fadfc7d07e96e72e38c55650514ecef750`.
`model/upstream_definition.yaml` preserves upstream metadata.

| Tensor | Shape | Type | Scale | Zero point |
| --- | --- | --- | --- | --- |
| Input, NHWC MFCC features | `1,49,10,1` | int8 | 0.20109497 | -5 |
| Output, 12 class scores | `1,12` | int8 | 0.05110505 | -63 |

Features represent one second of audio with 40 ms frames and 20 ms stride.
Raw PCM is not a model input. Dequantize scores as `(q + 63) * 0.05110505`.
The actual graph has no softmax; its outputs are scores, despite the upstream
description calling them probabilities. Class ordering is retained unchanged.

`model/model.py` preserves all 16 convolutions (including depthwise groups),
fused ReLU6, SAME padding, global average pool, reshape, int8 weights, int32
biases and original per-channel/per-tensor quantization parameters. It uses
standard ATen and `quantized_decomposed` Q/DQ operators. The exported input and
output are int8, so application tensor types must be adapted accordingly.

## Use with create_ai_layer

From the repository root, with the existing repository Python environment:

```powershell
uv run scripts/create_ai_layer.py --project kws_micronet_l
uv run scripts/create_ai_layer.py --project kws_micronet_l --quantize
```

Both commands consume `model/model_quantized.pt2` and
`model/model_quantized.safetensors`, then use ExecuTorch to lower to Ethos-U85
through Vela and generate the shared `ai_layer`. The second command calls this
project's `model/quantize.py`, which verifies and reuses the imported cache.
It never recalibrates or changes its weights. Missing or stale conversion
artifacts require the explicit conversion command below.

The shared `ai_layer` belongs to the selected application; these commands
replace it. Generating it does not build firmware or execute the network on
hardware. The network-only harness uses this KWS tensor interface directly.

## Reproduce conversion and host validation

TensorFlow is needed only for the comparison interpreter, in a separate
environment, so it does not alter the repository's Torch/ExecuTorch versions.
For example, create a Python 3.12 environment under `tmp/tflite-reference` and
install `tensorflow==2.20.0` there. With that environment available:

```powershell
.venv/Scripts/python.exe projects/kws_micronet_l/model/convert.py --reference-python tmp/tflite-reference/Scripts/python.exe
.venv/Scripts/python.exe projects/kws_micronet_l/model/validate.py --reference-python tmp/tflite-reference/Scripts/python.exe
```

These scripts also work from the project directory with `model/convert.py` or
`model/validate.py` and an appropriate path to the reference Python executable.
Conversion validates the in-memory export before writing the cache, then
validates the reloaded `.pt2` plus safetensors. `validation.json` records hashes,
test metrics and the synthetic seed. The test uses constants, extrema, a ramp,
checkerboard, boundary/center impulses, sinusoids, uniform noise and Gaussian
noise, and compares against both TFLite reference and default CPU kernels.
All 270 inputs are generated locally and temporary tensors are discarded.

The allowed final score error is two int8 quantization steps; PyTorch Q/DQ uses
floating accumulation/rounding while TFLite uses fixed-point requantization.
Restored cache outputs must match the Python import exactly. These are host
conversion checks, not a speech accuracy benchmark or an NPU runtime test.

The default CPU comparison additionally requires at most one quantization step
and identical top-1 predictions. The recorded 270-pattern result has 96.82% of
individual scores bit-identical to default TFLite, with 100% top-1 agreement.
Reference-kernel top-1 agreement is 269/270; maximum score difference is two
steps. See `model/validation.json` for both comparisons.

Run `.venv/Scripts/python.exe projects/kws_micronet_l/model/check_ai_layer.py`
from the repository root to exercise the real generator CLI in a temporary
solution, including `--quantize`, without replacing the shared AI layer.
`model/lowering_validation.json` records one Ethos-U85 delegate, no CPU
fallback operators and unchanged graph/weights. This check also generates and
checks the `.pte`, C array, header and CMSIS layer in that temporary solution.

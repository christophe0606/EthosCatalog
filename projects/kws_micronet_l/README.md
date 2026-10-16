---
description: Prequantized MicroNet Large keyword spotting through PyTorch and ExecuTorch; validated and benchmarked on Ethos-U85 with MRAM weights.
category: audio
---

# MicroNet Large keyword spotting

This project starts from `template_mram` and adapts the network:
the original quantized TFLite is imported as a PyTorch exported program and
quantized safetensors, ready for the existing ExecuTorch Arm backend to call
Vela. No training checkpoint, calibration data, or requantization is used.

The firmware runs a live CMSIS Stream keyword-spotting demo. `KWS_Debug` and
`KWS_Release` select it. Startup checks all 270 synthetic network vectors, then
extracts MFCCs from the reference `ks_down.wav` PCM and verifies the recognized
word before starting live capture. Startup checks are separate from live audio.

## Live Stream demo

The graph in [app.py](app.py) runs microphone -> PCM-to-float -> sliding audio
window -> generic MFCC -> sliding MFCC context -> inline Ethos-U85 inference.
MFCC output also feeds an event clock. Inference emits keyword events and the
clock emits redraw events to `KeywordLCD`, which runs only in the event thread.
Rendering and VSYNC waits cannot block the dataflow thread. Failed event posts
are counted. The LCD shows the latest accepted command and fades it to black
over 1,000 ms; another detection restarts the fade. Its 5x7 font occupies 130
bytes and is scaled while drawing, with no pre-rendered word images in MRAM.

`app.py` exposes `SAMPLE_RATE=16000`, `AUDIO_HOP=320`, and `MFCC_OVERLAP=24`.
The generated `MIC_SAMPLE_RATE` and `MIC_BLOCK_SAMPLES` configure the shared
DevKit-E8 microphone driver through the project hardware callback. Both spectrum
templates use the same `MicrophoneSource` node with their Python settings of
48 kHz and 512 samples. No software resampling is used.

MFCC uses a periodic 640-sample Hann window and a 1,024-point FFT, 40 HTK mel
filters from 20 to 4,000 Hz, natural logs, and 10 DCT coefficients. A 49-frame
context spans one second. In the reference `KwsProcessing.cc`, the audio-window
stride is half the 16,000-sample context, rounded down to a multiple of 320:
8,000 samples / 320 = 25 MFCC frames, hence 500 ms per inference. `main_live.cpp`
also replenishes half a second of mono audio per capture. The new graph retains
24 of 49 MFCC frames by default to match that cadence. In general:

```
inference period = (49 - MFCC_OVERLAP) * 20 ms
```

Set overlap to 48 for inference every new MFCC frame (20 ms). Regenerate the
graph after changing it. Inference is suppressed during startup and for a fresh
full context after an audio discontinuity. The first half-padded audio frame
never reaches a valid inference. Silence and unknown classes do not refresh the
display; commands require softmax probability >= 0.7. The model outputs logits,
so postprocessing applies softmax after dequantization.

The reusable [MFCC node](../../common/nodes_impl/c/MFCC.hpp) takes project-owned
tables from [mfcc_config.yaml](mfcc_config.yaml). It uses CMSIS-DSP primitives
rather than `arm_mfcc_f32`: the latter adds `1e-6` before the log, whereas the
reference uses `FLT_MIN`. The table generator also preserves the reference's
`arm_cos_f32` interpolation for Hann and DCT coefficients; using exact scipy
cosines changes low-energy tonal features. Host comparison against the unmodified
ML Eval Kit implementation checks silence, impulses, tones, noise, and all 49
speech frames: 145 frames / 1,450 coefficients match exactly after int8
quantization. [mfcc_validation.json](model/mfcc_validation.json) records the
reference hash and result. This is frontend equivalence on these inputs, not a
speech-dataset accuracy measurement.

Regenerate and check from the repository root:

```powershell
.venv/Scripts/python.exe projects/kws_micronet_l/generate_mfcc.py --dsp-scripts C:/Users/chrfav01/benchresults/cmsis/CMSIS-DSP/Scripts
.venv/Scripts/python.exe projects/kws_micronet_l/app.py
.venv/Scripts/python.exe tests/run_kws_tests.py
.venv/Scripts/python.exe -m unittest discover -s tests -p test_kws_graph.py
```

`app.py` and `generate_mfcc.py` also work from this project directory. The host
test defaults to the installed ML Eval Kit 22.8.0-Beta pack and the sibling
`mlek-cmsis-pack-examples/kws` reference; use `--pack-root` and `--reference` to
override those paths. It regenerates the startup speech fixture locally. No
speech dataset is downloaded. The host LCD check verifies queued delivery,
one-second fading, label bounds, and rejected-event counters.

Generate the KWS AI layer as described below, build `DevKit-E8@KWS_Release`,
then load/debug through CMSIS Developer Assistant. Inspect `pipeline_error`,
`app_result`, `npu_error`, the three `kws_*_test_passed` flags, and `stream_stats`.
Live progress includes audio/MFCC frames, inferences, presented frames, audio
and LCD deadline misses, rejected events, and capture discontinuities. Resume
the target after inspecting it.

The live demo was built and run on DevKit-E8 M55_HP on October 16, 2026.
Startup passed all 270 network cases (maximum 1 LSB), the MFCC fixture
(0 LSB), and recognition of the bundled "down" clip (99.92%). A live snapshot
recorded 4,508 audio/MFCC blocks, 173 inferences, 1,803 presented frames and
13 accepted commands, with zero pipeline/NPU errors, deadline misses or
rejected LCD events. Three capture discontinuities followed debugger halts;
the pipeline recovered and waited for fresh context each time. The user
confirmed live recognition and the one-second LCD fade. These observations
are recorded in [stream_validation.json](model/stream_validation.json),
separately from the historical network-only benchmark below. The final rebuilt
firmware also passed startup and a fresh 40.64-second run: 2,032 audio/MFCC
blocks, 80 inferences and 813 LCD frames, with zero capture discontinuities,
errors, deadline misses or rejected events. KWS Release was resumed and left running.

## Historical network-only board validation and benchmark

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

The archived harness is [network_benchmark.cpp](model/network_benchmark.cpp).
To reproduce those timings, use it in the project in place of `app_main.cpp`,
regenerate the KWS AI layer, select `DevKit-E8@KWS_Release`, and build and
load/debug through CMSIS Developer Assistant. Restore `app_main.cpp` for the
live demo. Capture UART4 at 115200 8N1 before the startup test runs. The harness
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

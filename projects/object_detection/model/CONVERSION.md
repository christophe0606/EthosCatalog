# YOLO Fastest face conversion

The original, already trained and quantized **pre-Vela** model is imported into
PyTorch as int8 weights, int32 biases and explicit quantize/dequantize operators.
The repository's shared cache writer saves `model_quantized.pt2` and
`model_quantized.safetensors`. ExecuTorch consumes that PyTorch cache and invokes
Vela for Ethos-U85-256. Neither original `_vela_H256` nor `_vela_Y256` model is
used. No architecture, weights, scales or zero points are retrained or calibrated.

## Source and interface

Public source: [Arm examples, object detection](https://github.com/Arm-Examples/mlek-cmsis-pack-examples/tree/main/object-detection).
Model origin: [Emza ModelZoo v1.0](https://github.com/emza-vs/ModelZoo/tree/v1.0/object_detection/).
Original FlatBuffer is 510,280 bytes, SHA256
`e94bcdb011784bead70ab0c0e9d2dae1a9ea5f103b43e1e6fac3019302cf71ab`.
`extract.py` accepts a checkout directory and extracts only this original array
and the 110,592-byte source RGB image. See `NOTICE.md` and the preserved Apache
license for copyright and reuse details.

| Tensor | Shape, NHWC | Type | Scale | Zero point |
| --- | --- | --- | --- | --- |
| Grayscale image | 1,192,192,1 | int8 | 0.00392117677256465 | -128 |
| Head 0, source tensor 276 | 1,6,6,18 | int8 | 0.13408391177654266 | 47 |
| Head 1, source tensor 283 | 1,12,12,18 | int8 | 0.18535925447940826 | 10 |

There are 113 original operations and 284 tensors: PAD, CONV_2D,
DEPTHWISE_CONV_2D, ADD, MAX_POOL_2D, CONCATENATION and
RESIZE_NEAREST_NEIGHBOR. `model.py` validates the pinned source hash, reads
all original constants/quantization, transposes weight storage into PyTorch
order, retains residual routes and preserves output order. The exported
interface remains NHWC int8, with no CPU float image boundary.

`patterns.preprocess` follows MLEK UC API 22.8.0-Beta `ImageUtils.cc`: convert
RGB to grayscale with float32 `0.299*R + 0.587*G + 0.114*B`, truncate to uint8,
then subtract 128. There is no normalization pass after that subtraction.
Live image crop/scale policy belongs to the application; avoid distorting
aspect ratio and map displayed boxes through the chosen crop/scale.

Each head stores three anchors per grid cell, six values per anchor in order
`x,y,w,h,objectness,face_class_logit`. Dequantize with `(q-zero)*scale`.
Head 0 anchors are `(38,77),(47,97),(61,126)`; head 1 anchors are
`(14,26),(19,37),(28,55)`, in 192-pixel model coordinates. The translated
`decode.py` uses sigmoid for center offsets and objectness/class scores,
`exp(width_logit)*anchor_width` and `exp(height_logit)*anchor_height` for
dimensions, score = sigmoid(objectness)*sigmoid(class), threshold 0.5,
one-class NMS IoU threshold 0.45, then clips to `[0,192]`. NMS uses the
unclipped floating boxes. MLEK's final integer geometry truncation is a later
presentation step. The class is face; this is not a general COCO detector.

## Representation details

SAME padding is computed from each source tensor shape; explicit source PAD
uses real zero, represented by the activation zero point. Spatial-pyramid
max pools preserve SAME semantics. Concatenation dequantizes each input with
its own original parameters before quantizing into the source output domain.

The source resize is fixed 6x6 -> 12x12 with `half_pixel_centers=true` and
`align_corners=false`. For each output index `i`,
`floor((i+0.5)/2) == floor(i/2)`, so ATen nearest gives exactly the source
indices. `representation_validation.json` records the exhaustive coordinate
mapping and scalar checks.

Source op 100 is a 1x1 convolution, 192 input channels -> 96 output channels,
with fused ReLU6 and output scale 0.18658526241779327, zero point -7.
ExecuTorch 1.4.1's `FuseQuantizedActivationPass` recognizes a conv+ReLU6 pair
only when its output zero point equals qmin (-128). To preserve NPU delegation,
this importer makes an additional Q/DQ boundary at the **same original output
scale** between that convolution and its clamp. Its quantized clamp interval
is [-7,25]. Zero maps exactly to -7; the quantized upper endpoint is 25.
`Q(clamp(x,0,6)) == Q(clamp(DQ(Q(x)),0,6))` for these parameters, including
saturation. All 43 full-model patterns were bit-identical before and after
this representation change; `check_representation.py` independently checks
200,779 scalar values, including both sides of every quantization boundary.
The workaround is project-local; shared exporter code is unchanged.

## Fidelity and accuracy scope

`validation.json` records the exact cache and script hashes, named patterns,
both heads, and decoded boxes for each real-image variant. The suite contains
43 inputs: the source RGB sample, mirrored/darkened/brightened/shifted versions,
five grayscale constants, horizontal/vertical ramps, fine/block checkerboards,
five corner/center impulses, and 24 uniform int8 random images, seed 20261019.
Synthetic patterns check numerical conversion; they are not calibration data.
The reloaded `.pt2` plus safetensors matches eager PyTorch **exactly** on every
output element for all 43 inputs.

| Host comparison | Head 0 max / mean LSB | Head 1 max / mean LSB |
| --- | --- | --- |
| Imported PyTorch vs default TFLite | 6 / 0.157479 | 3 / 0.071679 |
| Imported PyTorch vs BUILTIN_REF TFLite | 19 / 0.699864 | 11 / 0.364359 |
| Original default vs original BUILTIN_REF | 19 / 0.695701 | 11 / 0.360600 |

PyTorch and default TFLite have 87.39% / 93.15% bit-exact head elements;
their 99th-percentile differences are 2 / 1 LSB. The reference-kernel gap is
already present in the unconverted source interpreter. Float accumulation and
round-to-nearest Q/DQ and fixed-point requantization can differ at internal
boundaries; 113 quantized operations accumulate those small differences.
Raw output differences are reported rather than represented as lost accuracy.

The original source sample yields the same two face detections with identical
boxes and confidence scores in PyTorch and default TFLite (both IoU 1.0).
All five real-image cases have matching detection counts; the minimum matched
box IoU is 0.951424. Default TFLite vs converted acceptance gates are max 8 LSB,
mean 0.2 LSB per head, identical counts and IoU >=0.9 for every source-image
variant. These gates check bounded host fidelity and stable sample detections.
BUILTIN_REF remains a separately reported diagnostic baseline.

No labeled evaluation dataset or original float checkpoint was supplied.
The examined upstream model/demo descriptions provide no paired float-versus-
int8 error for this specific model. Its original quantization is retained;
`network.yml` therefore records `quantization: already` and `error: null`.
No new quantization error is measured, and no mAP, recall or dataset-accuracy
guarantee is claimed. Board/NPU fidelity and complete-demo behavior are later
validation stages.

## Reproduce

From the repository root:

```powershell
.venv/Scripts/python.exe projects/object_detection/model/extract.py --source-dir path/to/examples/object-detection
.venv/Scripts/python.exe projects/object_detection/model/convert.py --reference-python tmp/tflite-cpu/Scripts/python.exe
.venv/Scripts/python.exe projects/object_detection/model/validate.py --reference-python tmp/tflite-cpu/Scripts/python.exe
.venv/Scripts/python.exe projects/object_detection/model/check_representation.py
.venv/Scripts/python.exe projects/object_detection/model/generate_board_tests.py --reference-python tmp/tflite-cpu/Scripts/python.exe
.venv/Scripts/python.exe projects/object_detection/model/check_ai_layer.py
$env:TEMP = Join-Path (Get-Location) 'tmp'
$env:TMP = $env:TEMP
.venv/Scripts/python.exe scripts/create_ai_layer.py --project object_detection
```

The reference interpreter is an isolated Python 3.13 environment under ignored
`tmp/tflite-cpu` with `tensorflow==2.20.0`. It does not alter the repository's
Torch/ExecuTorch versions and is used only for host comparisons. The recorded
Torch version is 2.13.0+cpu. `quantize.py` validates and reuses the imported cache,
including when invoked by the shared generator's `--quantize`; it performs no
calibration. `check_ai_layer.py` exercises that real CLI in a temporary solution
and requires one delegate, no CPU operators, and unchanged graph/weights hashes.

## Board fixture handoff

The actual shared generator produced one Ethos-U85-256 delegate and zero CPU
operators, including PAD and nearest resize. The generated root PTE is 407,440
bytes, SHA256 `25c3a095d1a80d51ccdf8e3ac64b7d480ce63c93da519003da665c1219300244`.
`pte_sizes.json` extracts its exact delegate stream sizes: command data 19,552
bytes, weights 385,872 bytes, arena 527,504 bytes. The Vela console summary is
preserved as `conversion_vela_summary.txt`: 110 NPU operations, zero CPU
operations, 515.14 KiB SRAM and 376.83 KiB off-chip constants. These are compiler
sizes, not measured allocator peaks or runtime timing.

`lowering_validation.json` records the additional isolated real CLI check with
`--quantize`: one delegate, zero CPU operators, graph/weights unchanged. Its
407,472-byte PTE has a separate hash because the temporary solution's longer
configuration path is serialized in compile metadata. The shared root PTE and
its hash above are the artifacts to benchmark. No shared exporter was changed.
For initial board allocation, use a 64 KiB method pool and a 1 MiB temporary
pool, then record observed peaks and right-size if desired. A 512 KiB temporary
pool is insufficient for the 527,504-byte NPU arena. The arena is allocated from
the temporary pool, so do not add the arena size again when reporting peak RAM.

`model_test_data.h` embeds exactly four patterns: `source_sample`,
`constant_-128`, `horizontal_ramp`, `random_0`. The compiled constants total
186,336 bytes for inputs and all three host reference sets. Both original
output heads are included for default TFLite, BUILTIN_REF and restored PyTorch.
`board_test_vectors.json` contains the full host references and hashes.
Array names are `detection_test_inputs`, `detection_tflite_default0/1`,
`detection_tflite_reference0/1`, and `detection_pytorch0/1`.
The benchmark case is index 3 (`random_0`); time 100 forwards after warmup.
The board harness must independently record error metrics and the source
sample's decoded faces. The initial 8-LSB fixture tolerance is against default
TFLite; it must not be applied to the separate BUILTIN_REF diagnostic arrays.

Actual linked memory, allocator peaks, NPU time, complete forward time, camera
and display operation have not been measured in this conversion stage.

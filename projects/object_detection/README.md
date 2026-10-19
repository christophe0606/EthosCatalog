---
description: Live camera face detection on Ethos-U85 with CMSIS Stream, RGB565 preview and rotated LCD boxes.
category: image
---

# Object detection

Live face detection for Alif DevKit-E8 M55_HP / Ethos-U85-256. Seven CMSIS Stream
application nodes capture a camera frame, crop/convert the original quantized
input, run ExecuTorch, decode both heads, apply NMS, and display red boxes.
See [live validation](LIVE_VALIDATION.md), [network measurements](model/BENCHMARK.md)
and [model conversion](model/CONVERSION.md).

![CMSIS Stream graph](generated/app.png)

The Apache-2.0 source is Arm's
[object-detection example](https://github.com/Arm-Examples/mlek-cmsis-pack-examples/tree/main/object-detection),
using Emza's `yolo-fastest_192_face_v4`. Original INT8 quantization is preserved
through the repository's PyTorch / ExecuTorch / Vela flow: one Ethos delegate,
zero CPU fallback operators. Reused assets and notices are in
[model/NOTICE.md](model/NOTICE.md).
The existing Alif board platform retains its vendor license and Alif hardware
conditions, as detailed in that notice.

## Run

Select `DevKit-E8@Detection_Release` and enable **only** `object_detection.Release`.
The shared `ai_layer/generation.json` must identify `projects/object_detection`.
Build and load with the CMSIS Developer Assistant. UART4 is COM5, 115200 8N1.
Startup validates four network fixtures, benchmarks 100 forwards, then checks
the actual optimized C grayscale conversion and two-face decoder before starting
the live graph. `Test_result: PASS` covers the network; the subsequent
preprocessing/decode PASS and live-start message cover demo startup.

The default requests one frame every 200 ms (5 fps). Inference runs inline:
approximately 3.24 ms complete forward fits the measured full-frame budget.
The complete deadline includes sensor start/stop, capture, cache maintenance,
crop/grayscale, inference, decoding, rendering and the LCD release fence.
This cadence samples the sensor; it does not process every native sensor frame.

The E8 MT9M114 supplies 640x480 RGB565 using the project-gated camera profile.
The central 192x192 crop is expanded to RGB888, converted using the original
float32 `0.299 R + 0.587 G + 0.114 B`, truncated to uint8 then offset by -128.
Explicit intermediates preserve truncation under Release fast math. The native
color input replaces the original example's E7 Bayer sensor conversion while
preserving the RGB meaning of the network input.

Both NHWC heads `[1,6,6,18]` / `[1,12,12,18]` use the original anchors and
quantization. Objectness and combined class score must exceed 0.5; NMS IoU is
0.45. NMS operates before clipping boxes to `[0,192]`. The LCD uses an 800x480
landscape canvas. The same crop is enlarged 2x to 384x384, centered at logical
`(208,48)`, with black margins. Hold the panel in landscape: logical right maps
down its native 480x800 scanout, logical down maps toward native left. A single
clockwise transform maps image and boxes into the physical framebuffer.
Box-edge coordinates of 192 clamp to pixel index 191 when drawing.

## Ownership and diagnostics

There is one requested capture in flight. Snapshot completion and `Stop()` fence
DMA before CPU reads; the adapter rearms CPI configuration/interrupts for each
new shot. The next capture waits until the previous frame passed the LCD fence.
Two LCD buffers alternate after the prior scanout is released. Cache operations
surround DMA/scanout ownership transfers. Crop/grayscale/head/box storage belongs to the
processing nodes; `HardwareParams` carries the one initialized runtime and
physical framebuffer/input storage. Startup and live inference share model pools.

The camera and GT911 touch share I2C1. This demo uses `lcd_init_display()` so the
sensor owns the I2C completion callback. Existing spectrum projects keep
`lcd_init()` and their touch behavior. The camera adapter retains every error
notification in `camera_errors` and phase/mask details in `camera_diagnostics`;
startup MIPI notifications remain visible. Capture failures stop the graph and
publish `pipeline_error`, `stream_stats.error` and `app_result`.

Inspect `stream_stats` for stage progress, full-frame maximum, deadline misses,
capture timeouts and dropped/ownership counters. `dropped_frames=0` means no
requested capture was discarded. There is no queued inference event protocol.

## Regenerate and check

```powershell
.venv/Scripts/python.exe projects/object_detection/app.py
cd projects/object_detection
../../.venv/Scripts/python.exe app.py
```

Both working directories generate eight firings and 72 bytes of FIFO tokens:
camera, crop, generated view Duplicate, grayscale, network, decoder, NMS, display.
The Duplicate copies RGB view metadata for preprocessing and preview; it does
not copy image pixels. The crop retains RGB until display consumes it; grayscale
retains its int8 output, inference its heads, decoding its candidate array, and
NMS its sorted workspace/output until the next sequential frame.

Reusable `Rgb565CenterCrop`, `Rgb888ToGrayInt8`, and `NonMaximumSuppression`
descriptions live in `common/nodes/`, with portable C/C++ implementations in
`common/nodes_impl/c/`. They support other image dimensions, padded row strides,
box capacities and NMS thresholds without detector headers or board policy.
NMS preserves unclipped geometry and stable equal-score ordering. The project
owns capture sequencing, model-specific input/head/anchor decoding, clipping,
landscape drawing and cadence. The shared EmbeddedModule already supplies the
reusable inference runtime; the graph's FaceInference adapter binds this model's
two-head contract. Alif camera/display drivers stay in
`common/nodes_impl/alif_e8/`, and configuration in `board/DevKit-E8/`.

```powershell
.venv/Scripts/python.exe tests/run_detection_tests.py --vcvars path/to/vcvars64.bat
.venv/Scripts/python.exe scripts/validate_benchmarks.py projects/object_detection/model/benchmark.yml
```

The host runner also accepts `--cc clang` on a configured Clang host. It checks
all RGB colors, multiple rectangular crop/stride and NMS configurations, source
fixtures, NMS/clipping/rotation, production Stream wrappers and branch lifetimes,
exclusive DMA/display ownership, errors/deadlines, camera stop/rearm, and both
board profiles. Board execution is required after firmware changes.

Define `DETECTION_NETWORK_ONLY` to select the retained isolated startup harness.
It bypasses camera/LCD/Stream execution and uses the same single runtime/pools.
After any relink or timing change, capture UART through the CMSIS serial tools
and run `model/record_benchmark.py` with the actual build/execution job IDs.
These numerical checks do not measure labeled-dataset accuracy or mAP.

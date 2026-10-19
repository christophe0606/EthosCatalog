# Live landscape demo validation

Validated on the physical Alif DevKit-E8 M55_HP / Ethos-U85-256 on 2026-10-19.
Only `object_detection.Release` was selected. Final AC6 6.24 Release build
**b-22** was loaded by **l-23**, run by **g-24**, and inspected through the
matching CMSIS attach configuration. `get_device_info` identified
`out/object_detection/DevKit-E8/Release/object_detection.axf` and
`DevKit-E8@Detection_Release`.

ELF SHA256: `05a7474796f3c64ae052fb7a25ab64828d877f7240f886d4594fe016ff2220ae`.
The PTE and original quantization are unchanged. Raw startup evidence is in
[model/benchmark_uart.txt](model/benchmark_uart.txt); network values, timing and
hashes are in [model/board_validation.json](model/board_validation.json) and
[model/benchmark.yml](model/benchmark.yml). Live debugger snapshots are retained
in [model/live_validation.json](model/live_validation.json).

The independent reuse review found missing notice lines in the generated fixture
header and six active RTE forwarding adapters. Those were corrected without
changing declarations or fixture values. The fixture generator was rerun, and
the retained raw b-22/g-24 UART capture was rechecked against the regenerated
reference. [Notice equivalence](model/notice_source_equivalence.json) records
the observed compiled header hash, current header/generator hashes, and exact
reconstruction of the observed header by removing only the two new comment
lines. The validated ELF/PTE are unchanged; no new firmware run is claimed for
this notice-only correction.

Focused independent re-review closed the notice finding and checked the physical
target again: 5,769 captured/preprocessed/inferred/decoded/presented frames,
zero stream/pipeline/application, deadline, timeout, drop, ownership and display
errors. Raw camera initialization count remained 1/mask 0x40, with the other
phase counts zero. The matching Release target was resumed and confirmed running.
The [review report](../../.flow/object-detection/REUSE_REVIEW_REPORT.md) records
this later inspection separately from the retained 1,218-frame live snapshots.

## Behavior and scheduling

Seven application nodes run in eight scheduled firings: camera, crop, generated
shallow RGB-view Duplicate, grayscale, network, decoder, NMS and display.
FIFO tokens occupy 72 bytes on the target. Crop, grayscale and single-class NMS
are common portable nodes; model decoding, clipping, capture/display sequencing
and landscape UI remain project-owned. Inline
inference was selected from measured complete-forward latency, then validated
against the whole camera-to-display path. Default cadence is one requested
single-shot frame per 200 ms, approximately 5 fps; the sensor runs at its native
rate independently. No requested captures are queued or discarded.

The LCD uses an **800x480 landscape canvas**. The original square 192x192 model
crop appears at 2x scale, centered at logical `(208,48)` with black margins.
One clockwise transform maps logical `(x,y)` to native framebuffer
`(479-y,x)` on the 480x800 panel. Image pixels and red boxes use that same
transform; physical pixel `(0,0)` corresponds to logical bottom-left. This
preserves the source's square inference area while satisfying landscape display.

The MT9M114 profile is 640x480 RGB565. Live input memory, read through the CMSIS
debugger, produced a recognizable grayscale crop of the actual scene. This
checks capture and preprocessing continuity; it is not a camera calibration or
accuracy measurement. The observed detections count is model output on an
unlabeled scene, not confirmed face ground truth.

## Board checks

Both complete startup runs passed all four fixtures and 12,960 head values.
The compiled Release C grayscale function matched the independent source RGB
fixture byte-for-byte under `-O3 -ffast-math`. The actual C decoder reproduced
both source-sample boxes/confidences within 0.001 pixels / 0.00001 score.
Debugger flags for network, preprocessing, decoder and live startup were all 1.

The refactored run completed 1,218 frames. Between two halted snapshots, 885 complete
frames advanced over 177,037 RTOS milliseconds: **4.99896 fps**. At the final snapshot:

| Check | Observed value |
| --- | ---: |
| Captured / cropped / grayscaled / inferred / decoded / NMS / presented | 1,218 each |
| Pipeline / application error | 0 |
| Full frame maximum including LCD fence | 65,999,650 ticks = 164.999125 ms |
| Maximum through rendering before LCD fence | 54,537,737 ticks = 136.3443425 ms |
| Maximum live complete forward | 1,305,658 ticks = 3.264145 ms |
| Frame deadline misses / capture timeouts | 0 / 0 |
| Raw lifetime camera notifications / LCD error mask | 1 / 0 |
| Initialization notification count / mask | 1 / 0x40 |
| Idle / active capture / stopping notifications | 0 / 0 / 0 |
| Dropped requested frames / dropped events / ownership errors | 0 / 0 / 0 |
| Cumulative emitted detections | 1,243 |
| Application heartbeat / LCD scanline events | 244 / 15,049 |

The cadence interval advanced the application heartbeat from 67 to 244 and
LCD scanline events from 4,148 to 15,049. Pipeline/application errors remained
zero. The target was resumed after inspection; COM5 was closed, leaving CMSIS
Run g-24 and the matching attach session running.

The 400 MHz timer interval begins before sensor capture and ends after the
two-VSYNC LCD release fence. The 200 ms budget includes capture/start/stop,
cache maintenance, preprocessing, inference, decode, rendering and display wait;
the sink delays the remaining budget. Measured startup median NPU time is
3.01475 ms, complete forward 3.2407175 ms. The slower display/camera operations
determine full cadence, not inference alone. `dropped_frames=0` means no requested
single shot was discarded; it does not mean every native sensor frame was used.

The initialization-phase diagnostic contains one raw event with mask `0x40`.
It remained unchanged from 333 through 1,218 frames. Idle, active-capture and
stopping counts/masks were zero. This lifetime notification is retained rather
than filtered; no underlying CSI subtype is inferred from it. Earlier b-14
debugging observed two notifications before the first capture: breakpoints
interrupted `CSI2_PowerControl` and `CSI2_ConfigureHost` inside `camera_init`.
Their event argument was optimized out, so no exact old mask or CSI subtype is
claimed. Those notifications were counted, not filtered. They remained at two
through 353 otherwise successful frames. The pre-refactor b-19 run had zero
notifications; those historical reports are preserved separately in
`model/pre_reuse_baseline/`, including `record_b19.yml` and original raw UART.
Persistent phase/mask diagnostics now preserve any future notifications.

## Driver integration and memory

Touch and camera share I2C1. The pack I2C initializer keeps the first callback
when already initialized; initializing GT911 before the camera prevented the
sensor completion flag from updating. The detector uses `lcd_init_display()`;
the existing `lcd_init()` still initializes GT911 for spectrum projects.

Ensemble 2.2.1 `CPI_StopCapture` disables CPI interrupts and resets the
controller. The reusable adapter restores CPI configuration and events before
each single shot. Snapshot completion plus `Stop()` fence DMA before CPU reads.
Clean/invalidate operations bracket camera DMA; LCD publication cleans its
buffer and waits for prior scanout release. One camera buffer and two alternating
LCD buffers have exclusive owners. Node-owned RGB crop, grayscale, heads,
candidate boxes and NMS workspace/output are retained through downstream
consumption. The generated Duplicate copies only RGB view metadata; it cannot
release or overwrite the crop array. The next producer run follows display.

[Build-artifact memory report](model/benchmark_memory.txt): SRAM0 execution
3,301,376 bytes of 4 MiB (3.15 MiB), plus 16 segment-padding bytes; MRAM load
occupancy 1,011,216 bytes. DTCM contains the separate 262,144-byte C++ heap,
32,768-byte application heap, 32,768-byte stack, 151,652-byte RW region and
3,296-byte nonsecure region. Startup and live inference share the single
64 KiB method / 1 MiB temporary pools; observed peaks are 40,464 / 527,504 bytes.

## Host checks and limits

`tests/run_detection_tests.py --vcvars path/to/vcvars64.bat` passed with MSVC
optimized host builds. It checks all 16,777,216 RGB colors, the original sample,
RGB565 crop/expansion at multiple rectangular dimensions/padded strides,
grayscale at different capacities, NMS capacities/thresholds and stable ties,
unclipped/zero-area boxes, four reference fixtures, empty/overlapping/all-540-candidate
decode cases, clipping and landscape rotation at all four boundaries, guard
margins, separate candidate/NMS equivalence, 100 production Stream wrapper frames,
shallow preview branch lifetimes, runtime injection, exclusive
DMA/LCD ownership, failures and deadlines. The actual camera C adapter is tested
against a driver double whose Stop clears configuration, including errors and
phase diagnostics. Both shared RTE camera profiles compile; default settings and
the existing LCD/touch entry point remain intact.

Graph generation passed from the repository root and project folder. Network
configuration validation, all 17 project-script tests, all 17 benchmark-schema
tests and all repository benchmark records passed. The catalog was regenerated.
AC6 was built and physically tested; the Clang linker heap override is supplied
but no Clang firmware build or board run is claimed. No labeled-dataset mAP or
general face accuracy is measured. The target is resumed and left running.

# Object detection board benchmark

Release startup network execution on Alif DevKit-E8 M55_HP and Ethos-U85-256
passed on 2026-10-19. `benchmark.yml` is validated with
`schema/benchmark.schema.yml`. Refactored landscape demo build b-22 used AC6 6.24.0
with `-O3 -ffast-math`; load l-23 and execution g-24 used CMSIS tools. Only `object_detection.Release` was
selected. Actual queried CPU, AXI/NPU and RTOS system timer clocks were 400 MHz.

| Measured resource | Result |
| --- | ---: |
| Median NPU active cycles, 100 forwards | 1,205,900 |
| Median NPU active time | 3.01475 ms |
| Median complete `module.forward()` | 3.2407175 ms |
| PMU cycles minimum / maximum | 1,205,403 / 1,206,127 |
| Compiled PTE, including constants/weights | 407,440 bytes |
| Driver weights region, within PTE | 385,872 bytes |
| Driver arena, within temporary pool | 527,504 bytes |
| Observed method / temporary allocator peaks | 40,464 / 527,504 bytes |
| Reserved method / temporary pools | 65,536 / 1,048,576 bytes |
| Caller-owned int8 input | 36,864 bytes |

Do not add the arena to its temporary allocator peak, or add weights to PTE
size. Peaks plus caller input are 604,832 bytes of network storage. These
figures exclude application buffers, C++ heap, RTOS storage, stack and camera/
display processing. `benchmark_memory.txt`, obtained with the CMSIS build
artifact tool, records the complete linked demo: SRAM0 execution region
3,301,376 bytes of 4 MiB (plus 16 bytes segment padding), including 1,536,000
bytes for two LCD buffers and 614,400 bytes for the camera buffer. MRAM load
occupancy is 1,011,216 bytes. DTCM separately contains a 262,144-byte C++ heap,
32,768-byte application heap, 32,768-byte stack and 151,652-byte RW region. The linker
allows 2 MiB of application code MRAM. The benchmark fixture constants and
unused pool capacity are included in linked harness storage.

The harness loads the method before timing and validates four inputs. It then
copies `random_0` into NPU-visible SRAM0, performs one immediate warmup and 100
forwards. The RTOS timer surrounds `module.forward()` and includes ExecuTorch
dispatch, NPU driver and PMU callback overhead. Input/output copies, tensor
construction, output checking, decoding and UART are outside the interval.
No debugger halt occurred in the measured loop. NPU PMCCNTR and the independent
NPU_ACTIVE event count agreed on every inference, one NPU invocation per forward,
zero overflow. Both complete startup runs from CMSIS Load then CMSIS Run are retained
in `benchmark_uart.txt`; the recorder validates the final complete run.

## Board fidelity

Both output tensors have their original NHWC shape/order: `[1,6,6,18]` and
`[1,12,12,18]`. `board_validation.json` preserves all 12,960 fixture values,
per-head/per-case numerical comparisons and decoded detections. Source sample,
constant -128 and deterministic random input are bit-exact versus default
TFLite and restored PyTorch. The horizontal ramp differs by max 7 / 3 LSB and
mean 0.729938 / 0.239583 LSB on the two heads. Across all four fixtures the
means are 0.182485 / 0.059896 LSB. The original 8-LSB maximum board gate was
maintained. An initially added host-only per-case mean 0.2 gate was removed
after examining the complete board outputs; the initial capture is retained
as `board_initial_uart.txt`. The board mean error is reported without hiding
that synthetic ramp difference or attributing it to a proven internal cause.

Source sample detections match default TFLite and PyTorch exactly:

| Face box `[x0,y0,x1,y1]`, model pixels | Confidence |
| --- | ---: |
| `[170.113007,61.199524,186.964111,82.800476]` | 0.917512051 |
| `[3.601769,33.478592,32.663387,72.863625]` | 0.661385055 |

Both matched IoUs are 1. All three synthetic fixtures yield zero detections
on board and all host baselines. BUILTIN_REF is reported independently: max
8 / 4 LSB, means 0.676698 / 0.385706 across fixtures; its confidence difference
is already present in the original host interpreter comparison. These are
numerical/sample checks, not labeled-dataset accuracy or mAP measurements.

## Compiler estimates and provenance

Vela 5.1.0 estimates 1,767,133 cycles / 4.4178325 ms. This is a compiler estimate,
separate from measured PMU and complete-forward time. Raw CSV and configured
bandwidth estimates are retained. `capture_vela_estimates.py` exports the
unchanged cached PyTorch model with intermediate artifacts enabled, without
replacing the active AI layer. The temporary export container is 407,552
bytes because its debug artifact path is serialized. Its 19,552 command bytes
and 385,872 weight bytes are bit-identical to the actual 407,440-byte board PTE;
`benchmark_vela_provenance.json` records both stream hashes and container hashes.

The actual PTE SHA256 is
`25c3a095d1a80d51ccdf8e3ac64b7d480ce63c93da519003da665c1219300244`.
The final demo ELF SHA256 is
`05a7474796f3c64ae052fb7a25ab64828d877f7240f886d4594fe016ff2220ae`.
The recorder verifies fixture, cache, original TFLite, PTE, ELF, harness and
project provenance, and saves hashes in `board_validation.json` / `benchmark.yml`.

## Reproduce

From the repository root, with the active AI layer owned by this project:

```powershell
.venv/Scripts/python.exe projects/object_detection/model/capture_vela_estimates.py
.venv/Scripts/python.exe projects/object_detection/model/record_benchmark.py --build-job b-22 --execution-job g-24
.venv/Scripts/python.exe scripts/validate_benchmarks.py projects/object_detection/model/benchmark.yml
```

`network_benchmark.cpp` is included by `app_main.cpp` for startup validation
and timing before camera setup. Define `DETECTION_NETWORK_ONLY` to bypass the
live demo and run the same isolated harness. The single initialized runtime,
input and pools are shared with the subsequent Stream inference node.

Use CMSIS MCP build/load on `DevKit-E8@Detection_Release`, open COM5 at 115200
8N1 before load, and preserve complete raw lines through `Test_result: PASS`.
Wait for startup completion before CMSIS Run, whose probe server resets the
core; allow its benchmark to finish before attaching/halting. Record the actual
new job IDs. Retest timing after relink or forward changes. The final measured
3.2407175 ms forward fits the 200 ms complete-frame budget; see
[live validation](../LIVE_VALIDATION.md) for the camera/LCD measurements.

The earlier isolated b-3/r-4 evidence is preserved in
`benchmark_network_only.yml`, `benchmark_network_only_uart.txt`,
`benchmark_network_only_memory.txt` and `board_network_only_validation.json`.
That build measured median NPU 3.0085875 ms / complete forward 3.23132 ms.
Final records identify the current demo ELF instead. No causal attribution is
made for the small timing variation between these linked/started runs.

The pre-refactor b-19/l-20/g-21 record, raw startup, memory and live snapshots
are preserved in `pre_reuse_baseline/`; its YAML is named `record_b19.yml` to
distinguish a historical record from active benchmark variants. That firmware
measured NPU 3.014635 ms / forward 3.238185 ms. The model/PTE is unchanged.

The final target is left running with CMSIS Run and an attach session. Stop
debugging and CMSIS Run through the CMSIS tools before loading another image.

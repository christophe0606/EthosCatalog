---
id: techniques-helium-trace-analysis
title: Benchmark on a board before detailed trace analysis
type: techniques
architectures:
  - armv8.1-m
  - cortex-m52
  - cortex-m55
  - cortex-m85
tags:
  - helium
  - trace-analysis
confidence: source-reported
reproducibility: concept
related:
  - techniques-profile-guided-helium-optimization
  - techniques-helium-instruction-interleaving
  - techniques-cortex-m-data-prefetch-and-cache-hints
sources:
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md#L1579-L1709
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md#L1744-L1889
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/tools/arm_tarmac_2_chrometracing.py
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/tools/arm_json_merge.py
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
aliases:
  - TARMAC trace profiling
  - ETM trace analysis
  - Cortex-M instruction trace workflow
prerequisites:
  - Benchmark and profile a representative workload on the intended physical board before considering Tarmac traces.
  - Confirm that the target, simulator, probe, trace plugin, and post-processing tools support the selected trace format.
  - Keep the executable, symbol file, and trace capture from the same build.
---

Start by benchmarking and profiling a representative workload on the intended physical board. Board measurements are the preferred basis for performance decisions because they exercise the actual processor, memory system, peripherals, and application configuration. Use available timers, PMU counters, ordinary profiling, generated-code inspection, and board debugging to narrow the problem. If instruction-flow detail is still needed, consider supported on-board ETM capture.

Tarmac traces are generated in a simulated environment and produce very large logs that are costly to store and post-process; tracing can also slow simulation. Use a Tarmac trace as the very last resort to investigate a specific problem that board measurements and the available simpler diagnostics have not explained. Simulator trace timestamps do not establish target-board performance. Use the trace to form a focused hypothesis, then confirm any resulting change by benchmarking on the board.

Trace support, plugin names, timestamp sources, and post-processing formats vary by model, probe, and tool release; verify the installed toolchain's documentation before choosing a capture workflow. On-board ETM trace and simulated Tarmac trace are different sources of evidence.

## Capture a bounded, representative interval

If Tarmac is still necessary as a last resort, reduce the problem to a minimal reproduction and capture only the representative interval needed to test the hypothesis. Delay trace start until the part of interest and stop capture after it; avoid tracing the entire periodic workload. The [ML/DSP front-end optimization case study](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md) uses the model parameter `TRACE.TarmacTrace.start-instruction-count` for delayed capture; confirm that the installed model and trace plugin expose it. Keep the workload and capture conditions stable when comparing builds, and record which executable and platform produced each trace. A trace from an FVP or AVH model can help inspect structural behavior, but the Arm-Examples/Helium-Optimization repository warns that those model timestamps are not cycle-accurate target timing.

## Match symbols and post-process consistently

Use the symbol file from the exact executable that generated the trace. Post-process with the public trace utilities appropriate to the input format, then inspect function or instruction-address attribution. Mapping PC ranges to source-level functions requires matching symbols; instruction-address bins can reveal a hot region hidden by function-level aggregation.

The [Arm Tarmac Trace Utilities](https://github.com/ARM-software/tarmac-trace-utilities/blob/main/doc/index.rst) provide call-tree, profile, and flamegraph views. The Arm-Examples/Helium-Optimization repository also provides [arm_tarmac_2_chrometracing.py](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/tools/arm_tarmac_2_chrometracing.py) to correlate a TARMAC log with a matching symbol file and emit Trace Event JSON, and [arm_json_merge.py](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/tools/arm_json_merge.py) to align captures around selected events. Check each script's accepted input and options before converting; input formats and capture support differ. In the viewer, inspect self time as well as inclusive duration to distinguish a function's own work from its callees.

When comparing captures, align the same semantic event or workload occurrence and use a common time basis. Retain source identity for each trace and do not combine unrelated windows merely because they have similar duration. Perfetto can display trace-event JSON; the [ML/DSP front-end optimization case study](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md) describes the legacy Chrome trace viewer as superseded.

## Interpret timestamps and sleep carefully

Trace timestamps may be emitted only at selected PC discontinuities or loop branches rather than for every instruction. ETM capture granularity and timestamp support depend on configuration. The [ML/DSP front-end optimization case study](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md) also notes that timestamps can freeze while the MCU sleeps in one capture configuration; absence of CPU trace activity does not establish accelerator elapsed time. Use trace views to form structural hypotheses and confirm timing conclusions on the intended target with an appropriate measurement method.

When halting and restarting would disturb relevant real-time interactions, integrated trace can observe execution while the target runs. The available flow and event detail, MVE-aware decoding, and timestamp precision depend on the device, trace configuration, probe, and tools. Distinguish on-target ETM capture from FVP model trace: simulator trace precision is a model property and does not establish cycle-accurate behavior on hardware. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 166, 173–174](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

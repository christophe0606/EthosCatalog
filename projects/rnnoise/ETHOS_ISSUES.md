# RNNoise: Ethos toolchain issues and reproduction evidence

This report records problems encountered while porting the ML-zoo RNNoise INT8
network to Alif DevKit-E8. It distinguishes an observed Arm backend compiler
failure from integration requirements, numerical limitations, and board setup.
No imported library or CMSIS pack was patched. Status: optimized lowering,
on-board model verification, sustained microphone/DSP/LCD operation and injected
capture-gap recovery passed. Review and final reviewed Release verification
are complete; the microphone/LCD application is running on the board.

| Measured Release result at 400 MHz | Initial CPU fallback | Corrected delegation |
| --- | --- | --- |
| Whole-forward median | 35.0491575 ms | 0.38663375 ms |
| Whole-forward maximum | 35.125485 ms | 0.3881275 ms |
| Summed NPU median | 0.00887625 ms | 0.15211875 ms |
| Ethos invocations per forward | 7 | 2 |
| Host parity, fixture and 160 recurrent frames | 0 LSB error | 0 LSB error |
| Method / temporary pool peaks | 392,968 / 864 bytes | 3,824 / 928 bytes |

Correcting delegation reduced total inference time by about 91 times. This
timing excludes feature extraction, synthesis and display. No speech-quality
score is inferred from these conversion tests.

## Environment and source

| Item | Observed version/configuration |
| --- | --- |
| Host | Windows 11, build 26200; Python 3.13.13 |
| PyTorch / torchao | 2.13.0 / 0.18.0 |
| ExecuTorch Python / embedded CMSIS pack | 1.4.1 / 1.4.1 |
| Vela | 5.1.0 |
| TensorFlow reference interpreter | 2.20.0 |
| CMSIS-Toolbox / Arm Compiler | 2.15.0 / AC6 6.24.0 |
| Board / NPU | Alif DevKit-E8; Ethos-U85, 256 MAC configuration |
| CPU / NPU clock | 400 MHz, measured on the board |
| Vela settings | Ethos_U85_SRAM_MRAM; Shared_Sram |
| Release compiler flags | -O3 -ffast-math |

[versions.json](model/diagnostics/versions.json) also records SHA256 hashes of
the installed compiler-pass/helper source files to identify the exact code.

Model: [Arm ML-zoo RNNoise INT8](https://github.com/Arm-Examples/ML-zoo/tree/fec0bb5bcd486eb2839cdb7d437d466067402a6a/models/noise_suppression/RNNoise/tflite_int8),
113,472 bytes, SHA256
`9c582545b7c13af44616c44b654f4fe721aa2585630b0ca173ca3589f6f11c2c`.
The original model, architecture, license, and fixtures are in
[model/upstream](model/upstream). The port imports its trained weights and
existing quantization into PyTorch, then uses automatic ExecuTorch partitioning
and Vela. It does not train the model or manually divide it into subnetworks.

## 1. Compiler failure: duplicate constant deletion in FuseEqualPlaceholdersPass

**Observed failure:** compiling the correctly annotated quantized RNNoise graph
raises `KeyError: 'constant_14'`. One 24,684-MAC Ethos partition had already
compiled before the following partition failed in the Arm TOSA preprocessing
pipeline. This is a host compiler exception, before board execution.

Relevant traceback, with paths relative to the installed ExecuTorch package:

```text
backends/arm/_passes/fuse_equal_placeholders_pass.py:118
    delete_constant_placeholder(self.exported_program, node)
backends/transforms/utils.py:197
    del exp_program.state_dict[target]
KeyError: 'constant_14'
```

**Trigger:** the imported graph reuses one scalar INT8 constant in the three
GRU expressions `1 - update_gate`. Source tensor 14 has value 127, scale
approximately 0.0078740157, zero point 0, representing real 1. The gates have
24, 48, and 96 channels. Multiple constant placeholders can refer to the same
state-dictionary target after partitioning. The equal-placeholder pass replaces
uses and calls the deletion helper for every placeholder. The helper deletes
the state entry unconditionally; another placeholder still naming that target
then encounters a missing entry. This explanation is based on the traceback
and inspection of the installed pass/helper, and should be confirmed upstream.

**Expected behavior:** equivalent/shared constants should compile without a
missing-key exception; deleting one placeholder must respect other references
to the same exported-program state target.

**Project workaround:** give the three subtraction gates separate broadcast-one
buffers of shapes `[1,24]`, `[1,48]`, and `[1,96]`. Their quantized values,
scales, and mathematical results are unchanged. Different shapes avoid the
problematic scalar fusion. After this rewrite, lowering succeeded with two
automatic Ethos delegates, a 217,048-byte PTE, and 87,678 Vela MACs.

**Reproduction from the repository root**, using the configured Python
environment and installed packages above:

```powershell
.venv/Scripts/python.exe projects/rnnoise/model/diagnostics/reproduce_shared_scalar.py
```

This uses a frozen pre-workaround importer, the preserved source model, the
same compile specification, and the guarded annotation helper. It does not
change deployed AI assets or access the board. The original failing compilation
is preserved in [shared_scalar_failure.log](model/diagnostics/shared_scalar_failure.log).
The standalone reproducer was also executed and independently reproduced the
same `KeyError: 'constant_14'` (exit status 1). Its complete sanitized output is
in [standalone_reproduction.log](model/diagnostics/standalone_reproduction.log).

Suggested upstream investigation: test shared state targets across partition
inputs, fusion of equal scalar constants, and deletion of aliased constant
placeholders. A guard against a missing key alone should not conceal a remaining
reference to deleted state.

## 2. Prequantized imports need Arm quantization metadata

An exported graph containing explicit Q/DQ operations and original INT8 tensors
compiled successfully but delegated only seven small regions. All nine dense
layers stayed on the CPU. On board, the complete forward took **35.0491575 ms
median**, while summed NPU time was **0.00887625 ms median**. This misses the
10 ms audio-frame budget. Delegate count alone therefore did not establish
meaningful NPU acceleration.

The installed backend's `CheckArmQuantized` rejected floating compute with
`Node was not marked as quantized in the Arm backend.` The importer had not
attached `node.meta['custom']['_arm_annotation_info']['quantized'] = True`.
Explicit Q/DQ boundaries alone were insufficient for these compute nodes.

The project now validates the actual Q/DQ inputs and outputs, including the
supported fused ReLU pattern, before using the backend's
`mark_node_as_annotated` helper. It also checks integer structural nodes and
asserts on unexpected floating computation. Weights, scales, zero points and
arithmetic are preserved. Saved PT2 metadata was inspected after serialization.

With these annotations and the scalar workaround, all nine dense convolutions,
sigmoid/tanh, multiplication and subtraction are delegated. Only add, ReLU and
boundary Q/DQ remain as CPU operator components. Vela reports 24,684 + 62,994 =
87,678 MACs; the source dense layers account for 86,952 MACs. See
[annotated partition evidence](model/diagnostics/annotated_partition.json).
This is an importer/backend integration requirement and a diagnostic/documentation
problem; it is not evidence of an Ethos hardware defect.

## 3. Unsupported CPU linear operator and scale tensor dtype

The first import used PyTorch `linear`. Automatic partitioning left
`aten::linear` on the CPU, but the selected embedded pack exposes no matching
operator component. AI-layer generation failed with
`Runtime pack has no operator components for ['aten::linear']`.
The importer expresses each dense layer as an equivalent 1x1 convolution using
the same trained values. This preserves model functionality and has a supported
CPU fallback as well as an Ethos lowering path.

Replicated per-channel scale tensors initially used float64 and were rejected
by the Arm partitioner. They now use the source storage dtype, float32. This
correction alone did not resolve dense fallback; issue 2 was also necessary.
The original diagnostic log predates that correction and must not be read as a
description of the final graph. These are representation/pack compatibility
issues, not confirmed compiler crashes or hardware defects.

## 4. Allocation failure before any NPU invocation

The initial benchmark reserved a 128 KiB method pool. Its graph planned a
353,264-byte tensor buffer. Method loading failed with ExecuTorch error
`0x21` (`MemoryAllocationFailed`) and no NPU invocation. The original wrapper
error alone looked like a failed forward; permanent adapter load/runtime error
codes exposed the actual stage and error.

A project-owned 512 KiB method pool and 256 KiB temporary pool resolved it.
The CPU-fallback baseline measured peaks of **392,968 method bytes** and
**864 temporary bytes**. This was a firmware resource configuration error,
not an NPU crash. After the delegation fix, measured method/temporary peaks
fell to 3,824/928 bytes; the same larger reserved pools remained in place for
that comparison.

## 5. Numerical differences: source TFLite fixed-point vs imported Q/DQ

The source TFLite reference interpreter reproduces its supplied fixtures
exactly. The imported PyTorch graph is not bit exact to that interpreter.
The first localized difference is one LSB at tensor 111, a requantization from
tanh scale 0.0078125 to a coarse concatenation scale about 0.2215006053.
TFLite fixed-point two-stage rounding and ideal Q/DQ rounding differ; recurrent
feedback amplifies this. Intermediates before that point matched the source
fixture, with no architecture/order discrepancy found.

On the deterministic 160-frame synthetic sequence, independent recurrent
feedback gives gain MAE **0.0239591**, maximum **0.14453125**, and VAD MAE
**0.00576172**, maximum **0.078125**. With shared source state for each one-step
comparison, gain MAE is **0.00734419**, maximum **0.0625**, and VAD matches
exactly. The CPU-fallback board matched the imported host exactly on that fixture
and sequence. These source/import differences do not demonstrate an Ethos
arithmetic defect. The optimized NPU board graph also matched the imported host
exactly for all five outputs on the fixture and 160-frame sequence. Source
gain/VAD errors were unchanged from the baseline. Both actual NPU invocations
were observed per measured forward, with no PMU overflow or runtime error.

See [CONVERSION.md](model/CONVERSION.md) and
[conversion_metrics.json](model/conversion_metrics.json). The original FP32
checkpoint/error was unavailable. No preserved PESQ score or speech-quality
result is claimed from synthetic parity testing.

## 6. Other integration problems encountered

- The C++ Arm evaluation-kit frontend uses 512-sample frames and corresponding
  pitch constants, while this ML-zoo model specifies 480 samples at 48 kHz.
  Copying it unchanged would alter feature extraction. Exact frame/pitch/FFT
  matching and reference tests are required in the final application.
- The original Python frontend's silence branch returns two values where its
  public caller unpacks seven. Host validation consumed the available feature
  results without editing the imported source. This is a source frontend issue,
  separate from the Arm compiler failure above. For example,
  `RNNoisePreProcess(False).process_frame(np.zeros(480))` reaches a
  `ValueError` while unpacking the two-value silence result. The full DSP test
  harness repairs that return contract in memory only to exercise synthesis;
  the preserved upstream file remains unchanged.
- A first debug session used old YAMNet symbols although RNNoise firmware had
  loaded. A user-requested clean `cbuild --rebuild` and fresh session resolved
  it; the corrected session identified the RNNoise Release ELF. No runtime
  claims were made from mismatched symbols.
- COM3 and COM7 captures returned no benchmark text. Correct-symbol debugger
  inspection supplied the actual measured results; no UART capture is claimed.
- A shell rebuild initially could not execute `armar.exe` inside the sandbox.
  The same authorized command succeeded after execution approval. This was a
  host execution restriction, not a compiler or Ethos failure.
- A host reproduction initially could not write Vela's temporary `out.tosa`
  under the sandbox's default temporary directory. A process-local writable
  temporary directory inside the workspace avoids that host permission issue.
- The first complete application passed model startup checks, then raised
  `std::bad_alloc` during the frontend's first FFT allocation. The live stack
  identified the inherited 96 KiB C++ heap reservation; the board still had
  available memory. Increasing only RNNoise's linker heap reservation to
  **256 KiB** resolved it. The next live interval processed 813 complete
  inference/synthesis frames without errors. A subsequent fresh b-19/d-20 run
  processed **3,005 complete frames**, discarded **1,442,400 enhanced samples**
  and displayed **301 LCD frames**, with all four startup flags set and no
  pipeline, capture, deadline or LCD-drop errors. Maximum observed whole-frame
  time was **7.8051275 ms** at 400 MHz, below the 10 ms interval; total reserved
  RAM was 2,220,916 bytes and ROM 814,824 bytes. Tracked
  [live evidence](model/diagnostics/live_release.json) and its
  [full debugger capture](model/diagnostics/live_release_raw.json) preserve the
  counters. This is separate from the earlier ExecuTorch
  method-pool limit and is not an Ethos arithmetic failure.
- Deliberately halting the live target produced one PDM overflow, one capture
  discontinuity and one coherent recurrent reset; processing resumed with
  pipeline error zero and no LCD drops. Initially, resetting by reconstructing
  the DSP processors also rebuilt FFT tables, causing one **10.1400675 ms**
  recovery frame. The continuous pre-halt maximum remained 7.8051275 ms. Reset
  now clears histories while retaining constant transform tables. Host resets
  remain bit-exact to fresh state. Final Release b-21/d-22 recovered from three
  injected PDM overflows/gaps with three coherent state resets, 3,167 complete
  synthesis frames and 317 LCD frames, zero pipeline errors or deadline/drop
  counters, and maximum full-frame time **7.825190 ms**, including recovery.
  ROM was 814,952 bytes and RAM remained 2,220,916 bytes. This was a transient
  application reset cost, not an Ethos failure. The
  [original gap evidence](evidence/debugger_gap_recovery.json) and
  [corrected reset evidence](model/diagnostics/history_reset_live.json) preserve
  both results; the [full final capture](model/diagnostics/history_reset_live_raw.json)
  also records the one in-flight frame at the debugger halt.

The completed host DSP checks compare all 160 original frontend frames: maximum
feature error 0.019570768 and mean 0.000145998; 6 of 6,720 quantized feature
values differ by one LSB, with no larger difference. Synthesis with a fixed
0.25 band mask matches the original Python path with maximum normalized error
0.000019911 and mean 0.0000020039. Unity/mask attenuation, zero silence and
reset-versus-fresh checks pass. These validate the port's DSP implementation;
they do not measure noise-suppression quality on a speech dataset.

## Why the original TFLite path does not expose the same failure

The original TFLite reference interpreter was actually run: it reproduced every
provided input/output fixture exactly. ML-zoo also marks this model as supporting
Ethos-U. We did **not** independently run a direct TFLite-to-Vela firmware on
this board, so that support declaration is not a comparative hardware result.
The tested deployed route is PyTorch -> ExecuTorch -> Vela -> Ethos.

| Problem | Original TFLite representation/execution | Imported ExecuTorch route |
| --- | --- | --- |
| Shared scalar compiler exception | A FlatBuffer tensor/buffer index can be reused by several operators; the reference interpreter does not run Arm's FX placeholder-fusion pass. | Export/partitioning introduces graph-signature placeholders and a state dictionary. The recorded failure is deletion of an already-removed state target in that pass. |
| Missing quantization annotations | Tensor scales/zero points and operator quantization are native FlatBuffer metadata. | Imported Q/DQ arithmetic does not automatically supply the backend-specific ArmAnnotationInfo metadata required for delegation. |
| CPU linear component unavailable | The source has native FULLY_CONNECTED operators, successfully exercised by the reference interpreter. | PyTorch linear became an unsupported embedded CPU remainder; an equivalent convolution expression provides the needed representation. |
| float64 auxiliary scale tensors | Original FlatBuffer scales are float32. | Replicating them using Python/PyTorch's wrong constructor dtype introduced float64; this was an importer representation error. |
| Recurrent rounding differences | The original reference kernels apply integer fixed-point requantization, including intermediate rounding. | The reconstructed graph explicitly dequantizes and requantizes; ideal Q/DQ rounding can differ even with identical stored parameters. |
| Method-pool allocation failure | The original reference interpreter uses its own tensor allocation strategy. | ExecuTorch plans its own buffers and needs a sufficiently large project-owned allocator. No comparative TFLite embedded-memory measurement was made. |

The shared-scalar exception is therefore localized to the ExecuTorch Arm graph
transformation, before Vela compiles the failing partition and before any Ethos
execution. Its absence from a TFLite interpreter run follows from that pass not
being on the interpreter's path. It does not prove that every direct TFLite/Vela
configuration is bug-free. Likewise, fixing a successful-but-poor partition
requires inspecting which operations were delegated, rather than relying on
the source model's Ethos support label.

## Evidence and final verification

The [diagnostics directory](model/diagnostics) preserves sanitized failing and
successful compiler logs, partition reports, versions and the reproducer.
Host-specific paths in copied logs are replaced with placeholders. Original
source notices are preserved. The baseline on-board measurements and hashes are
in [BENCHMARK.md](model/BENCHMARK.md) and [benchmark.yml](model/benchmark.yml).

The optimized graph has been rebenchmarked, and the complete microphone/DSP/LCD
application passed a sustained interval of 3,005 frames and three injected-gap
recoveries after the reset fix. Review is complete, including source notices
and reproducible graph generation. The final reviewed Release build b-23 and
fresh debug load d-24 used the matching RNNoise AXF. Before any live-audio halt,
the application processed **4,687 complete frames** (46.87 seconds of audio),
discarded **2,249,760 enhanced samples** and completed **469 LCD updates**.
All four startup flags passed, including exact imported-host parity for all
five outputs on the fixture and 160-frame sequence. Pipeline errors, capture
gaps, PDM drops/overflows, audio/LCD deadline misses and LCD drops were all zero.
Observed maximum whole-frame processing was **7.805175 ms** at 400 MHz;
ROM/RAM reservations remained 814,952/2,220,916 bytes. This uninterrupted run
complements the earlier 7.825190 ms maximum including injected-gap recovery.
The [final reviewed Release evidence](evidence/final_reviewed_release.json)
contains normalized results and debugger responses. Inspection can itself
interrupt capture; these counters were captured at the first audio halt.
The target was resumed and left running with no breakpoints.
No Ethos hardware crash or NPU lockup has been established in the investigations
recorded so far.

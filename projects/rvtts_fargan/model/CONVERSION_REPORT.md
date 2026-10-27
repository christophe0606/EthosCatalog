# rvTTS FARGAN conversion

Source is [rvTTS](https://github.com/ArmstrongSubero/rvTTS), revision
`b30af35f5366197f1a8845ef55af01a6c7c39a64`. This variant contains the shared
prosody/acoustic conversion documented in
`../../rvtts_lpc/model/CONVERSION_REPORT.md`, plus a complete PyTorch FARGAN
neural frame and initialization method. Saved `.pt2` and `.safetensors` model
files are lowered by the repository ExecuTorch/Vela flow and its automatic
partitioner. The combined project-owned `../ai_layer/` package now contains a
2,608,624-byte PTE with six Ethos delegates.

## Why there are six methods

The original solution does not have six independent learned models. It has a
prosody network, an acoustic network and the FARGAN vocoder. This port exposes
four methods for the two shared networks (`prosody_pre`, one paired
`prosody_gru` step, `prosody_head`, and an `acoustic` tile) and two methods for
the vocoder (`fargan_init` and one complete 10 ms `fargan_frame`). The method
boundaries provide variable utterance length, bounded acoustic memory and
explicit recurrent state. A whole-prosody fixed-length trial compiled, but
`torch.export` specialized the GRU length; it did not support arbitrary UART
text lengths through that graph. FARGAN's complete neural frame remains one
method, with a separate initializer because its initial state computation
differs from subsequent frames.

Within every method, the ExecuTorch Ethos-U partitioner selects the delegated
operations automatically. The conversion does not hand-assign CPU/NPU subgraphs
inside FARGAN. The six method boundaries are scheduling interfaces rather than
six original models or six manually selected NPU partitions.

## Provenance

Selected source files retain their Apache or BSD notices. No vendor startup,
GPL code or restricted preprint is included. The generated FARGAN data in
rvTTS has no individual file header, so its provenance was checked against the
official [Opus 1.5.2 release](https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz).
All 66 arrays present in the official model match exactly, including int8
matrices, scales, biases, pitch embedding and small float matrices. The
official BSD-3-Clause license is preserved as `upstream/OPUS-COPYING`.
`official_provenance.json` records array matches and selected file hashes;
`check_provenance.py` reproduces the check without installing a full external
project. rvTTS's additional float matrix representations are covered by its
root Apache-2.0 license. Their relationship to the official int8 grids is
inspectable using `model.py`.

## Functionality and accuracy

`validate.py` builds a scalar float C reference from the original neural and
synthesis function bodies, preserving their LUT activations. PyTorch uses
standard tanh/sigmoid. On a 40-frame source-feature sequence, the maximum PCM
error is 0.000306 and RMSE 0.0000365 (reference RMS 0.01025); initialization
state maximum error is 0.00267. This establishes the mapping and bounds the
activation substitution on the fixture. It does not certify speech quality.
Fixtures and measurements are in `source_fixtures.npz` and
`float_equivalence.json`. No training is performed.

The first PTQ pass uses symmetric int16 activations and per-channel int8
weights. MinMax calibration uses four synthetic source-feature sequences of
2/7/32/96 frames with a 10% activation range guard. A separate 173-frame
sequence evaluates complete state feedback. Initial closed-loop PCM SNR is
7.38 dB (RMSE 0.01080, reference RMS 0.02526), which is a material quality risk.
Do not infer demo quality from these synthetic features. The final metrics
and any bounded calibration adjustment are recorded below and identically
in `conversion_metrics.json` and `network.yml`.

The baseline saturation diagnosis finds reciprocal-gain values as high as
239.43 against an int16 quantized range ending at 200.26, plus clipping in
several initialization projections. The single corrective trial increases
only the activation range guard from 10% to 50%; weight quantization and graph
structure stay unchanged. Closed-loop PCM SNR improves to 13.11 dB, RMSE to
0.005584 and maximum error to 0.03739 (same 173-frame sequence, reference RMS
0.02526). Individual-call frame maximum error remains 0.3192, RMSE 0.009724;
initialization maximum error is 0.03250. Wider ranges mitigate clipping but
recurrent feedback and weight rounding still change the waveform. This
bounded trial is frozen without broader optimization. The baseline metrics and diagnosis are retained as
`baseline_conversion_metrics.json` and `baseline_quantization_diagnosis.json`.

If the quantized vocoder is unsuitable after listening, a concrete comparison
option is the original scalar float FARGAN vocoder on Cortex-M, while retaining
the shared NPU prosody/acoustic methods. Its float weights total about 3.27 MB.
Alternatively the complete `Fargan` and `FarganInit` PyTorch models can be exported
for CPU ExecuTorch. `fargan_float.safetensors` preserves their parameters and
pitch embedding. Neither CPU option has established board real-time
performance. Do not substitute it silently in an NPU benchmark.

## Compiler checkpoint

The complete six-method automatic ExecuTorch/TOSA/Vela trial finished with a
lowering error after about eight minutes. The shared four methods compiled;
the vocoder fails in integer TOSA serialization:

```text
ValueError: TOSA-1.0+INT+int16+int4+cf doesn't support tensor
TosaArg(name='aten_view_copy_default_12_fused_const', dtype=FP32, shape=(1, 256, 1))
ValueError: Failed processing buffer placeholder:
aten_view_copy_default_12_fused_const. Is the original torch function supported?
```

The shape suggests the initialization's constant zero pitch buffer, but that
location is an inference and needs confirmation in the lowered graph. This is
one concrete backend/partitioning issue, not evidence that FARGAN cannot run on
Ethos. No hand decomposition or imported-library changes were made. A focused
follow-up can express initialization zeros through an explicit quantized
boundary or adjust automatic partition eligibility, then retry the complete
graph. The failed trial used the original 10% guard caches; the final 50% guard
caches were recompiled after the focused fix below. `compiler_trial.json`
preserves the failed checkpoint; `compile_trial.py --timeout-seconds 600` can
reproduce a bounded full-project attempt. The LPC layer is independently
complete.

The final saturation diagnosis finds no clipped int16 activation on the
worst init or frame calls. Remaining waveform differences therefore cannot be
attributed solely to the original range clipping. Full state-feedback and
speaker listening validation remain necessary after lowering is fixed.

## Focused initializer follow-up

The initializer now takes a 964-value zero state as an explicit third input.
This preserves the source result while preventing a floating zero tensor from
being serialized as a fused constant inside the integer TOSA partition. The
40-frame source-equivalence test is unchanged. A one-method trial on the final
50% guard cache **passes** automatic ExecuTorch/Vela lowering as one Ethos
delegate with 311 NPU operators and a 959,008-byte PTE. The full frame method
also **passes** as one delegate, 376 NPU operators, and a 1,121,904-byte
one-method PTE. The combined six-method application has not yet passed. The
method contract and `compiler_init_trial.json` / `compiler_frame_trial.json`
record the changed interface and individual results.

The subsequent six-method trial lowered all methods through Vela, then stopped
while selecting ExecuTorch runtime components: the pack provides `Portable
to_dim_order_copy` but the project AI-layer generator did not map the operator
name `dim_order_ops::_to_dim_order_copy` to it. The generator now includes that
alias. A final full run generated `../ai_layer/model.pte` with six Ethos
delegates, 2,608,624 bytes and the required Portable/Quantized operator
components. `../ai_layer/generation.json` records the model and cache hashes.
This establishes compilation, not board runtime, real-time performance or
audible speech quality.

## Reproduce

From the repository root:

```powershell
uv run projects/rvtts_fargan/model/check_provenance.py
uv run projects/rvtts_fargan/model/validate.py
uv run projects/rvtts_fargan/model/quantize.py --project rvtts_fargan
uv run projects/rvtts_fargan/model/diagnose_quantization.py
uv run scripts/create_ai_layer.py --project rvtts_fargan
```

Host reference build products go to `tmp/rvtts-reference-fargan/`. On restricted
Windows sessions, set `TMPDIR`, `TEMP` and `TMP` to a writable directory under
this repository before running Vela. The adapter scheduling and state layout
are specified in `ADAPTER_CONTRACT.md`. Actual board timing and listening are
required before claiming a working real-time demo.

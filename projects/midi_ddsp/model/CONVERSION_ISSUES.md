# MIDI-DDSP conversion findings

## Noncausal context and sequence normalization

The source frame-level pitch context has a bidirectional 256-unit GRU. The
expression context has a bidirectional 128-unit GRU. The harmonic/noise decoder
has twenty symmetric residual convolutions plus an input convolution. DDSP
`Normalize('layer')` computes statistics over time and channels for the whole
segment; replacing it with per-frame Keras/PyTorch LayerNorm would change model
behavior. The current float conversion preserves both normalization variants
with their original epsilon (DDSP 1e-5; Keras .001).

The complete graph currently exposes fixed complete segments. This is not proof
that an exact one-frame causal implementation exists. A later bounded block
player must account for context, normalization, latency, and boundary quality.

## First full-graph PTQ quality failure

The first all-operator a16w8 PTQ diagnostic exported successfully to pre-Vela
`.pt2`/`.safetensors`, but quality is unacceptable. Across thirteen held-out
sampling-quantile sequences, maximum raw synthesis error was .0790651,
expression error .0117179, pitch-logit error 10.8480 and sampled MIDI-pitch
error 1.99219 semitones. See `ptq_complete_validation.json` for each instrument.
These are rejected diagnostic results, not accepted model accuracy.

The categorical top-p sampler uses a -1e10 exclusion sentinel and +1000 threshold
sentinel. Quantizing these nonlearned operations is suspected of contaminating
activation ranges shared with the pitch logits. A second diagnostic keeps the
entire sampler module in float and uses MinMax activation observers. It retains
the original sampling distribution and learned checkpoint. This quantization
policy still lets ExecuTorch partition the complete model automatically; it is
not manual separation into CPU/NPU subnetworks. The hypothesis is not considered
confirmed until the second diagnostic's actual comparison is inspected.

## Graph scaling

The complete static model has 1,446 graph nodes at four frames, 3,126 at sixteen
frames and 9,846 at sixty-four frames (two notes throughout). Float graph
parameter storage remains 19,881,780 bytes because weights are shared. Static
autoregressive loop unrolling adds 140 graph nodes per frame in these probes.
The 1000-frame case would therefore have about 140,886 nodes by extrapolation;
that case has not been exported or benchmarked. Small export cases do not
establish realistic block capacity. `export_scaling.json` records actual probes.

## Current evidence limits

Float framework validation passes all 39 combinations of instrument IDs 0–12
and sequence lengths 4, 17 and 129. Maximum raw-control difference is
2.78950e-5, expression difference 9.83477e-7, pitch-logit difference 3.05176e-5,
and sampled pitches are identical under shared uniform quantiles. The reference
uses independent TensorFlow Keras GRUCells and TensorFlow math with the original
checkpoint tensors, after auditing the upstream architecture. It does not run
the full original training/analysis package or compare final waveform audio.

## Automatic full-graph backend failure

The first complete quantized graph was passed to the normal
`scripts/create_ai_layer.py --project midi_ddsp --quantize` path. It partitioned
automatically and compiled an initial delegate (Vela reports 8,750,522 MACs),
then failed in `TOSABackend._preprocess_module` / `ArmPassManager` with:

```
TOSA spec TOSA-1.0+INT+int16+int4+cf only supports torch.int8 weights for
torch.int16 input but found torch.int16 (TOSA op: DEPTHWISE_CONV2D)
```

The actual terminal failure stack is preserved in
`tmp/midi_ddsp/complete_partition.log`. This demonstrates that the automatic
complete graph was attempted before any manual partition. The failed graph
also has unacceptable PTQ quality and is not an accepted deployment artifact.
The corrected automatic attempt keeps normalization and categorical sampling
in float to avoid both sensitive PTQ operations and the suspected quantized
normalization/scale backend path. A backend failure in the corrected graph
has not yet been established. Imported ExecuTorch/Vela code has not been edited.

Standalone replay of a saved Arm-quantized graph must register
`executorch.exir.passes.dim_order_ops_registry` before `torch.export.load`, since
the Arm preparer inserts `dim_order_ops._to_dim_order_copy` for integer indices.
An initial project-owned replay script omitted that import; this was corrected.
That missing import is not evidence of an unsupported model or backend failure.

The corrected complete attempt terminated with a different issue: Windows
sandbox `PermissionError` while opening `out.tosa` in Python's default temporary
directory, then another permission error during cleanup. This is an environment
failure before Vela compilation, not evidence of incorrect network math. Its
stack is in `tmp/midi_ddsp/corrected_partition.log`. The standard generator CLI
already sets `tempfile.tempdir` inside the repository; invoking its internal
function directly had bypassed that CLI setup. The practical method export uses
the standard CLI, with no imported library modifications.

## Reusable-method conversion after the automatic failure

The documented full-graph backend failure permits a faithful method split.
`deployment.py` retains eight shared learned methods and all original trained
weights. The scheduler retains both bidirectional passes, original state
updates, exact CPU categorical sampling and full1000frame harmonic normalization.
All13 split/full1000frame float comparisons passed the1e-4 maximum-error gate.
`deployment_float_parity.json` contains measured per-instrument errors.

Each method still uses ExecuTorch's automatic operator partitioning. Sensitive
normalization modules are left float by quantizer policy; they are not manually
extracted into replacement algorithms. Calibrated a16w8 graph/tensor pairs are
saved before Vela compilation. Early long closed-loop PTQ results have larger
logit trajectory differences than single-step tests; shared-quantile categorical
boundary changes feed back into future recurrent states. Teacher-forced held
state/context comparisons are required to distinguish local distribution error
from stochastic trajectory divergence. These metrics do not by themselves
establish acceptable final waveform quality.

`DEPLOYMENT.md` describes the exact four-second method ABI, original segment
initialization, fixed instrument, padding/context limitations, all CPU work and
buffer lifetimes. A four-second segment is not mathematically equal to arbitrary
whole-song inference. Musical seams and end padding still need validation.

No Vela PTE, board accuracy, real-time inference or audio playback result has yet
been accepted. Host DSP oracle comparisons are tracked independently in `dsp/`.

## Inference preprocessing omissions found during review

The first composed port expanded raw generated expression directly into
synthesis. Upstream `expression_generator_output_to_conditioning_df` defaults
to clipping those six conditioning values to[0,1]. The expression model's
autoregressive feedback must remain its raw, rest-masked output. The initial
synthetic score also assigned relative position1/length..1 to rests; upstream
`get_note_mask_from_onset(...,note_on_only=True)` leaves that feature zero on
rests. Its default100-region limit leaves later regions zero too.

These were project conversion/fixture bugs. Shared TF/PyTorch arithmetic tests
using the same handcrafted inputs could not detect a shared frontend omission.
Review independently exercised AST-extracted, unmodified upstream helpers in
`player/validate_features.py`: all four fixtures passed with zero error,
including a rest-only1000frame segment and102 regions. The project frontend
oracle is `player/score_features.py`; no original package dependency tree was
installed. The composed PyTorch model, independent TensorFlow reference and
shared schedule now clip synthesis conditioning only, and calibration fixtures
set rest relative position zero. Raw expression feedback stays unchanged.

The provisional6,218,656-byte PTE, its metrics and shared-method caches were
archived under `tmp/midi_ddsp/diagnostic_before_frontend_fix/` and superseded.
They establish compiler feasibility only, not accepted player behavior. Corrected
float validation, calibration, conditional/closed-loop metrics and PTE generation
are being rerun. No baseline quality number is silently reused as corrected.

Re-importing the same float safetensors while another validation process holds
its mapped tensors fails on Windows with OS error1224 (user-mapped section
open). It does not imply a checkpoint incompatibility. `convert.py --export-only`
now re-exports the corrected complete float graph without replacing imported
weights; re-import weights only after users of the old mapping have exited.

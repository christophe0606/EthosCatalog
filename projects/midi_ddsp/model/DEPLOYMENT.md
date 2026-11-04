# Faithful block method schedule

This is a conversion/benchmark interface, not a claim of working firmware.
`deployment.py` executes the full original expression and synthesis equations
using reusable learned methods. The original complete automatic graph was
attempted first and failed as recorded in `CONVERSION_ISSUES.md`. Each saved
method is still automatically partitioned by ExecuTorch; there is no hand-picked
CPU/NPU placement of its operators.

## Inputs and context

One invocation schedule renders one instrument, one monophonic score stream,
and exactly 1,000 frames at 250 Hz (4 seconds at 16 kHz audio/64 samples per
frame). Instrument ID is fixed for the whole schedule. A change of instrument
inside this interval requires another computational stream/schedule. A cap on
simultaneously sounding notes alone does not bound required model work.

The score supplies N note pitches (MIDI 0 means rest), N durations in seconds,
and a frame-to-note index. Per-frame features are pitch/127, onset indicator,
offset indicator and relative note position, in that order. Onset and offset
are one on the first and last frame of the note. Relative position is
1/length through 1 on sounding notes and zero on rests. The original helper's
default mask limit also leaves relative position zero for score regions with
index100 and above; callers must preserve that behavior. The sampler
supplies one uniform quantile per frame. The caller must implement the original
top-p=.95 distribution in float, including ties at its threshold; replacing it
with argmax or fixed pitch changes the model.

Both bidirectional contexts see the complete supplied score segment. All GRU
states and previous-expression/previous-one-hot vectors begin at zero. Backward
cells traverse the sequence in reverse and their outputs are restored to
forward order before concatenation. Expression is masked to six zeros on
rest notes before feeding it back. Expression conditioning is separately
clipped to[0,1] before frame expansion;
the raw masked expression remains the autoregressive feedback. The harmonic
network's normalization uses
all 1,000 frames and channels, including rests. Its symmetric convolutions use
the original zero edge padding. Do not normalize a frame or tile separately.

At the end of a file the fixed-length model must receive an explicit rest
score through the remainder of the block, with consistent duration, mapping
and frame features. Zero padding the harmonic input is not equivalent. The
current numerical fixtures are four-note synthetic segments, including a rest;
they do not validate arbitrary end padding or seams between independent blocks.

Four-second blocks preserve the selected training-length normalization window,
but independent blocks do not equal arbitrary whole-song inference. Whole-song
normalization, bidirectional context and autoregressive initial state differ.
Continuing recurrent state or overlapping/crossfading blocks would need its own
explicit comparison. No boundary-quality result is claimed by this conversion.

## Calls, packing and CPU math

All method inputs/outputs are contiguous float32. Shapes below are `[1,width]`,
except harmonic `[1,1000,width]` and the current precondition method
`[16,10]` to `[16,256]`. Its compiled board checks and timing are recorded in
[BENCHMARK.md](BENCHMARK.md).

| Method | Input width | Output width | Calls per block |
| --- | ---: | ---: | ---: |
| expression_forward | 320 = embedding192 + hidden128 | 128 | N |
| expression_backward | 320 | 128 | N |
| expression_step | 518 = context256 + previous6 + hidden128 + hidden128 | 262 = expression6 + hidden128 + hidden128 | N |
| precondition | 10 = expression6 + frame features4, per row | 256 per row | 63 batches of16 |
| context_forward | 576 = precondition256 + instrument64 + hidden256 | 256 | 1000 |
| context_backward | 576 | 256 | 1000 |
| pitch_step | 1225 = context512 + previous onehot201 + hidden256 + hidden256 | 713 = logits201 + hidden256 + hidden256 | 1000 |
| harmonic | 384 = precondition256 + instrument64 + pitch embedding64 | 126 | 1 |

The current schedule makes3,001 + ceil(1000/16) + 3N executions
(3,076 forN=4), with1,000 valid preprocessing rows and8 discarded padded rows.
Padding applies only to independent preprocessing rows; it does not pad the
harmonic time window or change recurrent state semantics. Weights, quantization
parameters and last-axis moments are unchanged. Host rowwise-linear evaluation
is bit-exact with the prior one-frame method; native batched host GEMM fails a
separate tight comparison gate due to rounding (max0.00041556). That gate is
recorded in precondition_batch_validation.json. Compiled held errors and the
instrument-0 valid-frame trace match the prior one-frame board baseline. The
maximum observed neural schedule is 2.85898343 seconds for four seconds of
controls; all thirteen identities completed. The one-frame baseline makes
4,001+3N executions and takes 4.52 seconds. Both exclude audio DSP/output, and
the harmonic provisional numerical gate and startup reliability remain open.
CPU work includes learned
pitch/instrument embedding lookup, duration affine, frame mapping, all packing
and state feedback, exact categorical sampling, sampled pitch = score pitch +
index*0.01 - 1, and the learned affine embedding of pitch/127. These small learned
tensors and all 13 signed 48,000-tap reverb IRs are in `cpu_assets.bin`;
`cpu_assets.json` records offsets. The default address is 0xC1800000; the PTE
must end before that address and both assets must fit the 32 MiB partition.
Float normalization and any other fallback inside a method remain part of its
ExecuTorch execution and must be included in measured inference time.

The DSP pitch conversion preserves the upstream exact-zero test: MIDI pitch
equal to zero maps to 0 Hz; every other value maps to 440*2^((pitch-69)/12).
Negative sampled rest residuals therefore remain about 8 Hz. Rest amplitude
behavior remains learned.

Using separate packed input/output arrays at every boundary transfers about
17.512 MB per block for the four frame methods plus harmonic, before note
methods, score preparation and DSP. This is an interface byte count, not a
measured bus cost; aliasing/reuse must be established by the actual runtime.

## Buffer lifetime plan to verify

Float32 precondition+instrument (`z`) occupies 1,280,000 bytes. Frame context
occupies 2,048,000 bytes while pitch generation runs. After pitch generation,
the context storage can be reused for harmonic input (1,536,000 bytes); the
1,280,000-byte z sequence must remain until that input is constructed. Harmonic
output occupies 504,000 bytes. Recurrence input/output, pitch and score arrays
are additional. The original whole-window statistics remain in the harmonic
method, whose compiler-planned buffers and all delegate scratch are additional.

`inspect_program.py` records actual serialized method buffers and Vela scratch.
These are not linked firmware totals. Loading multiple methods/delegates may
retain metadata. An audit of the pinned ExecuTorch1.4.1 runtime confirms that
`EthosUBackend::execute` allocates scratch from the temporary allocator;
`runtime/executor/method.cpp` resets that allocator after delegate execution.
Serial execution can therefore reuse one delegate scratch pool. The actual
T1000 harmonic planned buffer is3,072,000bytes and maximum delegate scratch
is1,216,000bytes. Their sum4,288,000bytes exceeds one4MiB bank; the banks must
be placed separately. Summing harmonic delegate scratch18,295,488bytes would
overstate the serial peak. Other CPU temporary allocations, method metadata,
caller inputs/outputs and DSP remain additional and require a runtime bound.
SRAM0 and SRAM1 are separate 4 MiB banks. A buffer
cannot straddle them just because their capacities add to 8 MiB. The benchmark
must establish placements, reusable lifetimes and runtime allocation success.

Audio DSP should consume the 126 controls and pitch in bounded 64-sample
intervals while the next complete model block runs. No complete PCM block is
required by this interface. Real-time acceptance requires all model calls,
CPU packing/sampling, harmonic/noise/reverb DSP and output to fit the block's
4-second production window with documented latency and margin.

# MIDI-DDSP conversion report

The original pretrained thirteen-instrument checkpoints have been imported
without training into `pretrained.safetensors` and reconstructed in PyTorch.
This is the learned expression model, MIDI pitch model and harmonic/noise
control decoder. All thirteen instrument IDs and learned signed reverb rows
are retained. The audio-analysis encoder and training discriminator are outside
the selected upstream MIDI inference path. Archive provenance and license
limits are in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

## Float fidelity

After correcting inference preprocessing, independent TensorFlow/Keras execution
of the original checkpoints agrees with PyTorch for all thirteen instruments
at 4, 17 and 129 frames (39 cases). Maximum absolute errors are 3.09944e-5 for
raw controls, 9.83477e-7 for expression and 2.67029e-5 for pitch logits. Sampled
pitch is identical under shared uniform quantiles. `float_validation.json`
isolates arithmetic from the different framework random number generators.

Independent source-helper frontend tests in `player/feature_validation.json`
report zero error. They cover clipped synthesis expression conditioning,
rests and the source's 100-region relative-position limit. Raw masked recurrent
expression feedback is covered by the network TensorFlow/PyTorch comparison.
`deployment_float_parity.json` compares all thirteen
full 1,000-frame reusable-method schedules with the composed float model under
a maximum absolute error gate of 1e-4.

## PTQ and deployment

The pre-Vela artifacts are eight `*_quantized.pt2` and companion
`*_quantized.safetensors` files. Quantization uses ExecuTorch's Ethos-U symmetric
int16 activation/int8 per-channel weight configuration. Activation range is
[-32767,32767], zero point zero, MinMax observation, epsilon 2^-24 and a 25%
calibration range guard. Original float normalization and categorical sampling
are retained. The helper's default per-channel weight MinMax epsilon remains
2^-12. Bias scales are derived by the quantizer. There is no QAT or retraining.

Calibration uses thirteen synthetic four-second/1,000-frame score trajectories
with note transitions and rests. At most 24 well-spaced held inputs per
instrument are retained for shared recurrent/dense methods; all thirteen full
harmonic input blocks are used. Calibration data is less than 100 MB. It is
not held-out URMP accuracy or a perceptual listening evaluation.
For instrument ID i the pitches are [48+i,55+i,0,60+i], each lasting one second.
Calibration uses uniform seed1100+i; evaluation uses seed9100+i. Thus sampling
quantiles differ while the four-region score pattern is shared. Wider note
ranges, duration patterns and dense onsets remain outside these local metrics.

`deployment_conditionals.json` records local method errors across all thirteen
instruments, including held-state pitch distribution KL/total variation,
expected pitch shift, expression controls and raw control components. Its
explicit synthetic local gate is suitable for proceeding to board benchmarking;
it does not establish final music quality. `deployment_ptq_validation.json`
records completed separate 1,000-frame closed-loop comparisons for all thirteen
instruments. Shared-uniform sampled trajectories can diverge after a
categorical boundary flip, so assess their distribution/logit errors alongside
held-state conditional errors, pitch statistics and final audio.

`deployment_batch16_ptq_validation.json` repeats all thirteen complete host
schedules with the current saved batch16 ABI; all outputs are finite. The
original one-frame schedule goldens also regenerate bit-exactly using held
one-row arithmetic. `waveform_diagnostics.json` renders the current closed-loop
corpus using the source-checked CPU oracle, shared white noise and full signed
learned reverb. It separately reports independent sampled pitch paths and a
held reference oscillator pitch. Across three FFT sizes, spectral convergence
is 3.71–31.13% for independent paths and 2.83–9.01% with held oscillator pitch;
these are descriptive measurements with no perceptual pass or relaxed gate.
Raw controls still retain differences from previous sampled pitch/context.
The corpus uses the synthetic four-region register, and establishes neither
embedded DSP nor natural-register listening or seam quality. Reproduce with:

```text
python projects/midi_ddsp/model/validate_waveforms.py
```

All thirteen corrected local benchmark cases pass. Across instruments, worst
mean original-top-p total variation is 0.00548664 (0.5487% probability mass),
worst mean untruncated KL is 0.000114371, and worst mean absolute expected pitch
shift is 0.0278409 cents. The worst single held-frame expected shift is 2.37393
cents. Full-block held-input harmonic RMSE/reference RMS ranges from 0.4885%
to 0.8227%; expression-control RMSE ranges from 0.00401 to 0.00447. These are
synthetic conditional/control measurements, not a final sampled-audio score.

The separate eight-note/rest demo phrase uses each instrument's ordinary
register. The current-cache rerun in `deployment_batch16_demo_conditionals.json`
reproduces twelve passing identities and the tuba failure: mean absolute
expected pitch shift is 0.120217085 cents against the unchanged 0.1-cent gate.
Tuba top-p total variation is 0.00684908 against 0.01; every demo identity's
held-input harmonic NRMSE remains below 1%. This failure is retained separately
from the NPU harmonic-output maximum-error warning and is not a listening test.

The resumable `validate_closed_loop.py` check completed all thirteen cases:
outputs are finite, worst raw-control normalized RMSE is 3.6154% and pitch
RMSE ranges 1.485-9.689 cents. `pitch_variability.json` records two additional
original-float categorical seeds per instrument; this small sample illustrates
sampling variation and does not establish statistical or perceptual bounds.

Additional held-input checks use the actual eight-note demo phrase and each
instrument's natural demo register, which differs from the calibration pitches.
`demo_conditionals.json` reports harmonic relative RMSE 0.359-0.673% and
pitch total variation below 0.007. Tuba mean expected pitch shift 0.120217 cent
exceeds the conservative 0.1 cent synthetic gate; the other twelve range
0.0338-0.0724 cent. This suite therefore fails its combined gate. Keep that result
visible when assessing broader calibration and final audio; do not describe
the original four-region pass as coverage of all demo registers.

The complete automatic graph was attempted first and failed with a concrete
TOSA dtype error. The documented faithful reusable-method split then gives
ExecuTorch eight methods to partition automatically. No individual operator is
manually assigned to CPU/NPU. [CONVERSION_ISSUES.md](CONVERSION_ISSUES.md) preserves
failures, preprocessing bugs, impacts and reproducer logs. The methods retain
the original full four-second normalization/context window. They do not claim
whole-song equality or causal single-frame operation.

## Embedded contract and evidence limits

The corrected eight-method export completed with 38 Ethos-U delegates in a
6,218,656-byte PTE, CRC32 0x566e34b6, SHA256
`d563c9e61872eee06f42153e5cd3e8530c5857e5543fea524cff7b99e3821e31`.
CPU weights occupy 2,536,448 bytes at 0xC1800000 and benchmark fixtures occupy
2,062,272 bytes at 0xC1C00000; all fit the same 32 MiB OSPI partition without
overlap. The generated layer registers the actual portable arithmetic and
quantize/dequantize fallback kernels. These are compiler/storage results only.

The weight-staging candidate regenerates the same method caches with
`extract_delegate_segments: true`. It contains 6,221,136 bytes, CRC32
`e9f5969d`, SHA256
`92be03a2be15307aaabc93d1a61c5ee86d52db49903c9b1d20a1a64e919aa1e9`.
Its 38 backend payloads are individually loadable; one 47,136-byte CPU constant
segment is separate. Asset addresses and CRCs are unchanged. Exact storage
equivalence passes for all eight methods in `storage_validation.json`: compiled
payloads, CPU constants and normalized execution plans are identical. The
`validate_storage.py` check reproduces this result. Staged board accuracy and
complete copy/load/inference timing are separate acceptance requirements.

[DEPLOYMENT.md](DEPLOYMENT.md) specifies every method, state, caller operation,
copy count, context and buffer lifetime. There are 4,001+3N method calls per
four-second instrument stream containing N score regions. Dense instrument
changes require computational-stream admission control as well as a sounding
voice cap. No complete PCM buffering is required.

`compiler_memory.json` records actual PTE planned buffers and each delegate's
scratch/weight/command sizes. The pinned runtime resets its temporary allocator
after delegate execution, permitting serial scratch reuse. Linked firmware,
extra temporary CPU allocations, bank placement and caller/DSP memory require
board evidence. `ai_layer/generation.json` binds the generated PTE to all method
caches and OSPI assets. `benchmark_fixtures.h` and `benchmark_assets.bin` contain
deterministic held-input quantized goldens; the latter includes the large
harmonic fixture, not PCM audio.

The corrected harmonic planned buffer is 3,072,000 bytes and maximum delegate
scratch is 1,216,000 bytes. They require separate SRAM banks; their sum exceeds
one 4 MiB bank. Individual frame methods use at most 8,208 planned bytes and
8,096 delegate scratch bytes. Caller sequences and other firmware allocations
are additional. `compiler_memory.json` includes constant and command byte counts
for the complete 4,001+3N call schedule.

Complete board neural timing and all thirteen identity paths are now measured
in [BENCHMARK.md](BENCHMARK.md). Final waveform/closed-loop quality, reliable
startup, block seams/end padding, streaming output and a measured voice cap
remain required. A compiler result or synthetic local PTQ gate is not a
real-time player acceptance result.

## Reproduction

The current compiled model batches16independent precondition rows.
Its generated PTE is6,221,424bytes, CRC32de624a9e, SHA256
e1cf5cc94363f68cc76dec54347d75472d36b7e200b92be33e59c5e0355a1d52.
The seven other methods retain exact compiled delegate bytes, CPU constants
and normalized plans (batch_unchanged_storage_validation.json). Precondition
planned memory is131136bytes and maximum scratch49152bytes; unlike harmonic,
its planned buffer uses the regular393216byte method pool, whose actual peak
is 190180 bytes. The old individual-frame memory counts above describe
the accepted pre-batch baseline.

Precondition state tensors and quantization parameters are unchanged. Across
33host cases covering all13identities and tails, identical rowwise linear
summation gives bit-exact output. Native host batched GEMM instead differs by
up to0.00041556 and fails the initial2e-5 comparison gate; this failure remains
reported, not relaxed. See precondition_batch_validation.json. Generated
heterogeneous16-row held goldens preserve original per-row linear arithmetic.
Compiled board checks retain the B1 held errors and instrument-0 valid-frame
trace. All thirteen identities complete; maximum observed neural time is
2.85898343 seconds per four seconds of controls. The B1 baseline takes 4.52
seconds after fast quantize/native dequantize optimization. These measurements
exclude CPU DSP/output; harmonic provisional accuracy and intermittent startup
failure remain open, and no playable voice capacity is established.

```powershell
.venv/Scripts/python.exe projects/midi_ddsp/model/convert.py --checkpoint-dir tmp/midi_ddsp/midi_ddsp_model_weights_urmp_9_10
.venv/Scripts/python.exe projects/midi_ddsp/model/tf_reference.py --checkpoint-dir tmp/midi_ddsp/midi_ddsp_model_weights_urmp_9_10
.venv/Scripts/python.exe projects/midi_ddsp/model/export_cpu_assets.py
.venv/Scripts/python.exe projects/midi_ddsp/model/quantize_deployment.py --project midi_ddsp
.venv/Scripts/python.exe projects/midi_ddsp/model/batch_precondition.py --batch 16 --activate
.venv/Scripts/python.exe projects/midi_ddsp/model/validate_conditionals.py
.venv/Scripts/python.exe projects/midi_ddsp/model/make_benchmark_assets.py
.venv/Scripts/python.exe scripts/create_ai_layer.py --project midi_ddsp
.venv/Scripts/python.exe projects/midi_ddsp/model/inspect_program.py
```

The checkpoint fetcher accepts a repository-local cache directory and verifies
the original archive hash before extraction. Source files are a minimal
Apache-compatible subset with notices preserved. No original package dependency
tree, GPL SoundFont or URMP dataset is installed.

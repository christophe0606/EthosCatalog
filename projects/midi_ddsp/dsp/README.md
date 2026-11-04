# CPU synthesis reference

`reference_synth.py` is a NumPy mathematical oracle for the waveform processing
after the learned MIDI-DDSP model. It is host code, not an embedded synthesizer
or a performance result. It accepts raw `[frames,126]` synthesis controls,
fundamental frequency in Hz, explicit deterministic white-noise samples and an
optional learned signed 48,000-sample reverb impulse.

`validate_reference.py` compares it with the unmodified Apache-2.0 DDSP
`core.py` copied under `reference/`. The exact upstream revision, source URLs
and SHA256 hashes are in `reference/provenance.json`. Only that math file and
its license were copied; the upstream package/dependencies were not installed.
An identity `gin` decorator is supplied in memory if `gin` is absent, without
changing the copied source. Run from the repository root:

```text
python projects/midi_ddsp/dsp/validate_reference.py
```

After model conversion, add `--cpu-assets projects/midi_ddsp/model/cpu_assets.bin`
to check all thirteen learned signed reverb rows as well. The checker verifies
the asset size/CRC and compares a multi-impulse response long enough to include
the late decay taps. This remains a host mathematical check.

The checked conventions are:

- 16 kHz samples and a 64-sample control interval; sixty harmonics and sixty-five
  noise bins.
- MIDI pitch conversion with the original exact-zero silence test. A negative
  sampled pitch residual during a rest is not automatically clamped to zero.
- Exponentiated sigmoid amplitude/harmonic scaling, Nyquist masking and harmonic
  renormalization. Noise uses the same scaling after subtracting five.
- Forward linear frequency interpolation with a held final interval; amplitude
  interpolation uses overlapping periodic Hann windows of length 128.
- Noise uses an IRFFT of length 128, the periodic Hann window in zero-phase
  form, and a causal shift. The upstream positional window size is 16,000 and
  clamps to 128. Input noise is filtered in non-overlapping 64-sample frames,
  with overlap-add and the original 62-sample group-delay crop. A streaming
  implementation must account for that bounded lookahead.
- Learned reverb coefficients are signed time-domain FIR taps. The envelope is
  unity for 16,000 samples, then `exp(-4*t)` across 32,000 samples. Tap zero is
  suppressed; wet causal convolution is added to the dry signal with unity gain.

The default host oracle accumulates harmonic phases in float64. A separate
`phase_mode="ddsp_float32"` reproduces the source's 1,000-sample float32
chunk/modulo order for the source waveform check. The report records the
precise-versus-source arithmetic difference separately, and checks interpolation
and control values independently. The current five-length result, through a
full 1,000-control-frame/four-second window,
is in `reference_validation.json`. It covers synthetic controls, a sparse signed
reverb fixture and all thirteen learned IR rows when `--cpu-assets` is supplied;
it does not establish listening quality or complete trained-instrument waveform
equivalence. Validate the firmware implementation separately against this
oracle, including persistent phase, filter overlap, reverb tails and boundaries.

The final firmware must generate bounded blocks and overlap audio output with
inference/synthesis. Neither this full-array host oracle nor its FFT allocation
strategy is a permissible whole-song PCM playback implementation.

`make_runtime_fixture.py` reads the independent learned-control LCG trace and
exports host-only numerical test arrays under `.flow/midi_ddsp/dsp_runtime_fixture`.
The current fixture contains one complete 1,000-frame violin block, explicit
white noise, both harmonic phase policies, filtered noise, the actual signed
reverb impulse and expected dry/wet signals, with shapes and hashes. It lets a
bounded C++ streaming implementation compare against real model values before
board profiling. These expected waveforms are validation data and must never
be linked into demonstration firmware or used as its audio source.

The production C++ backend is in `../nodes_impl/c/ddsp_synth.*`. Its host FFT
implementation passes the learned four-second fixture. The extended check also
covers all thirteen saved learned-control/IR cases, six lengths from one to
1,065 control intervals, Nyquist crossings, persistent phase/filter state,
the 750-partition reverb history wrap and complete signed reverb tails. It
tests raw learned-IR initialization, first-tap suppression and decay endpoints,
and checks output guards and input preservation. The optimized sine lookup
table passes this suite with maximum difference below 9e-6
under the unchanged 1e-5 tolerance. Boundary phase correction preserves the
sum of the oracle's float32 interpolated frequencies. The result binds the
tested C++ source hashes; it uses the host FFT and precise reference phase.

Run it after the saved closed-loop control corpus is available:

```text
python tests/validate_midi_ddsp_dsp_extended.py --vcvars <path-to-vcvars64.bat>
```

The source-bound result is [embedded_host_validation.json](embedded_host_validation.json).
The current CPU implementation also uses a 2,049-point cubic Hermite table for
the sigmoid/power amplitude transform over [-16,16). Values outside that range
use the source formula. Values and interval derivatives use 16,392 bytes total.
The complete host suite passes with this transform under the unchanged 1e-5
waveform tolerance; maximum observed difference is 8.92e-6. The final Release
firmware with this table and strict Helium phase calculation passes the target
probe: 2,560 selected samples, maximum wet error 1.79e-6 under the unchanged
1e-5 tolerance. The historical scalar timing below predates the amplitude table.
The [final integrated playback report](../model/stream_final_b91.json) records
uninterrupted completion of the included score; it is separate from DSP-only
profiling.
`model/make_dsp_probe.py` also generates deterministic synthetic probes for
target comparison. They cover 1,065 intervals and ten selected complete output
intervals, including a 750-partition history wrap and pitch/Nyquist transitions.
Expected samples are only test comparison data. The host checks the complete
probe waveform; the target must generate its own actual samples. CMSIS FFT,
board timing, actual firmware buffer handoff and independent neural-window
seams require separate validation.

`../nodes_impl/c/ddsp_reverb256.*` groups four
control intervals into one 256-sample reverb call. Its 512-point real FFT and
188 partitions retain every signed tap, including the final partial partition.
Each coefficient/history bank uses 385,024 bytes, 2,048 extra bytes in total
over the 64-sample version. Grouping adds at most 192 samples (12 ms) of
accumulation delay; the caller must include it in the playback latency.

[reverb256_host_validation.json](reverb256_host_validation.json) records host
comparison with full convolution, including all thirteen learned IR tails,
history wrap, reinitialization, in-place/disjoint output and buffer guards.
Maximum observed error is 1.20e-7 under the same 1e-5 tolerance. Reproduce with:

```text
python tests/validate_midi_ddsp_reverb256.py --vcvars <path-to-vcvars64.bat>
```

The Release board check with the current 8,192-entry linear oscillator and this
reverb backend passed all 2,560 selected target samples over 1,065 control
intervals: maximum harmonic error 6.38e-7 and wet error 1.79e-6. The four-second
violin DSP profile took 1.1583 seconds, including 0.9872 seconds for harmonic
and noise synthesis and 0.1636 seconds for reverb. Initializing the full learned
IR took 64.95 ms separately. Raw evidence is
[`dsp_board_linear_lut.json`](../model/dsp_board_linear_lut.json).
These are DSP-only measurements without SAI playback.

An initial four-harmonic Helium oscillator compiled with fast floating-point
transformations ran faster but failed the completed target accuracy probe
(maximum harmonic error 0.0577). That result was rejected. The current Helium
implementation uses per-file `-fno-fast-math -ffp-contract=off` and passes the
target probe; strict arithmetic preserves the phase correction. Its isolated
profile was slower than the earlier scalar oscillator, so the rejected fast
timing is not used as a performance claim.

Grouped final partial blocks must pad missing dry samples with zero, rather
than generating extra held-control samples. Firmware buffer handoff, audio
interrupt load and independent neural-window seams require separate checks.

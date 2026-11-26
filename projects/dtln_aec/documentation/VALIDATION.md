# DTLN-aec validation

## LCD replay verification

Both RGB565 framebuffers use 1,536,000 bytes of SRAM0 at `0x0226ADE0`.
SRAM0 usage totals 4,070,896 bytes, including the model and inference pools.
Build `b-26` and load `r-27` confirmed visible labels and moving waveforms
during the 30-second synthetic replay. All 3,756 hops met the 8 ms deadline,
with a maximum of 4.441 ms of work and 290 LCD updates.

Build `b-28` and load `r-29` rotated the landscape display by 180 degrees.
The user confirmed the labels were upright. The three recorded clips completed
all 4,081 hops with zero deadline misses, a maximum of 4.456 ms of work,
and 317 LCD updates. Both runs reported zero display errors. All four
captures matched the earlier SHA-256 hashes.

The earlier layout at `0x02400000` reported successful controller updates
while the physical LCD was black. The SRAM0 measurements above include
visual confirmation.

[Manual startup order](SDS_REPLAY.md#run-the-usb-demo-manually)

## USB signal processing results

ReleaseSRAM build `b-15` and load `r-16` ran the corrected PTE with CRC
`31C8406C`. The model quantizes the linear layers and keeps the LSTM
state and nonlinearities in float32. Both stages passed the integer TOSA
startup fixtures. [Conversion and calibration](CONVERSION.md#quantization-and-fidelity)

The continuous 30-second USB replay passed the signal regression checks
with one uninterrupted recurrent state per stage. The original comparison
uses the float DTLN-aec 128 TFLite models on the same input and alignment.

| Synthetic measurement | Corrected board | Original float |
| --- | ---: | ---: |
| Echo-only ERLE across six repeats | 4.40–4.94 dB | 3.74–4.63 dB |
| Near-end gain, repeat 1 | −1.02 dB | −1.07 dB |
| Near-end gain, repeat 6 | −1.49 dB | −1.61 dB |
| Double-talk error reduction, repeat 6 | 8.76 dB | 8.67 dB |

The 120-second continuous pre-Vela host run also passed the signal checks.
Its final near-end gain was −1.55 dB versus −1.62 dB for float.

On three independent recorded speech clips, waveform RMSE against the
original float output improved in every case.

| Clip | Previous board RMSE | Corrected board RMSE |
| --- | ---: | ---: |
| Far-end only | 0.0002034 | 0.0000460 |
| Near-end only | 0.0487987 | 0.0084066 |
| Double-talk | 0.0225268 | 0.0031717 |

Recorded far-end-only reduction remained close to the original at 49.6 dB
versus 49.4 dB. On the dedicated test runs, the board completed all
3,756 synthetic hops and all 4,081 recorded hops with valid timestamps
and zero misses against the 8 ms budget. The worst completed hop was
4.505 ms, including board-side SDS access. An earlier recorded replay
overlapped host validation jobs and had three timing outliers. It still
captured every record.

## USB results before the accuracy fix

The DevKit-E8 ran `DTLN_AEC_ReleaseSRAM` with PTE CRC `5AE153B6`. The
synthetic USB replay contained six successive five-second episodes in one
stream. Both the board and the original 128-unit float TFLite model carried
their recurrent state across the entire replay. The comparison aligned the
waveforms and measured the center 500 ms of each one-second section.
[Reproduction steps](SDS_REPLAY.md#measure-echo-cancellation-with-a-known-clean-signal)

| Repeat | Echo-only ERLE, board / float (dB) | Double-talk error reduction, board / float (dB) | Near-end gain, board / float (dB) |
| --- | ---: | ---: | ---: |
| 1 | 25.77 / 3.74 | 13.11 / 12.97 | -0.57 / -1.07 |
| 2 | 22.79 / 4.23 | 11.39 / 10.08 | -1.12 / -1.48 |
| 3 | 24.00 / 4.63 | 10.51 / 9.00 | -3.78 / -1.54 |
| 4 | 22.25 / 4.63 | 10.40 / 8.73 | -28.00 / -1.58 |
| 5 | 23.78 / 4.59 | 10.21 / 8.67 | -29.38 / -1.60 |
| 6 | 22.97 / 4.57 | 10.13 / 8.67 | -29.29 / -1.61 |

Near-end RMS on the board dropped from 0.07180 in repeat 1 to 0.00263
in repeat 6, with clean RMS 0.07671. Both tested silence windows stayed at
zero on the board in every repeat. All 3,756 SDS hops arrived with the
expected timestamps, and the production graph recorded zero misses
against the 8 ms hop budget. The worst completed hop took 2.022 ms.
The near-end suppression makes this model unsuitable for sustained
echo cancellation as measured on this pattern.

The saved pre-Vela quantized methods also lost near-end level on the same
sequence in the host validation loop. Gain reached -9.40 dB in repeat 6.
The attenuation therefore appears before Vela compilation and was more severe
in the deployed PTE.

On the three recorded speech clips, the board reduced far-end-only
mic RMS by 45.71 dB and the original by 49.40 dB. In the near-end-only
clip, board output was 2.33 dB below the input mic RMS and the original
was 0.07 dB below it. In the double-talk clip, board output RMS was
1.34 dB below the original output. These recorded clips lack isolated
clean near-end ground truth, so the double-talk error-to-clean measurement
comes from the synthetic test.

## Earlier synthetic streaming demo

ReleaseSRAM build `b-11` and load `d-12` passed on Alif DevKit-E8.
This report records the earlier synthetic replay firmware and its board
measurements. The current USB replay procedure is in the [SDS test guide](SDS_REPLAY.md).

## Graph and bounded storage

The graph keeps the synchronized fake mic and reference source separate from
processing and the fake sink. Both source outputs use 128 float samples per
hop. Shared SlidingBuffer nodes retain the 384 preceding samples. CPU FFT and
magnitude nodes produce the two spectral inputs to the stage1 method node.
The mask/inverse-FFT node preserves the original mic phase. A separate stage2
method node receives that estimate and the reference time window.
OverlapAdd emits each 128-sample result immediately to the sink.

The two purple method nodes refer to the same PTE. Each owns a separate
512-float previous state and next-state buffer. No node stores an utterance.
The generated schedule has 14 firings including fork nodes and reserves
9,220 bytes of FIFO storage. No LCD, microphone or speaker is initialized
by the project. The fake endpoints can be replaced with transport nodes.

The reference FFT uses PreservingFFT, which copies 512 input samples before
calling CMSIS-DSP. The generator shares the reference time window between
the FFT and stage2 branches, while CMSIS-DSP's forward fast RFFT modifies its
input. The private 2,048-byte copy preserves the time samples for stage2.
Other callers of the existing FFT node retain their current behavior.

## Timing and scheduling

The nominal hop interval is 8 ms at 16 kHz. Source pacing uses RTX absolute
tick deadlines. A late source sleeps one tick so the application reporter
can run, then rebases its next deadline. It preserves every input hop and
both recurrent states. If the deadline expires during the absolute-delay
call, the source checks the current tick and recovers that race. Genuine
delay API errors propagate to the runtime.

Both methods execute inline for the SRAM-staged configuration. Its guarded
CPU kernels and SRAM0-resident PTE leave headroom within the 8 ms hop.
The direct-OSPI configuration is retained for comparison.

Stage counters measure full dtln_forward adapter time, including its copies
and ExecuTorch call. The complete processing interval starts after source
pacing and includes pattern generation, windowing, FFTs, both methods,
inverse FFT, overlap-add and fake-sink metrics. It excludes source sleep and
UART reporting. Deadline misses compare that processing interval with 8 ms.
The application thread prints cycle summaries outside the timed dataflow.

## Synthetic checks

The fake source repeats five one-second sections of silence, echo only,
double talk, near-end only and silence. A source period is 80,000 samples.
The source marks a new input epoch at the silent replay boundary. Both
recurrent states start from zero for each independent test episode and carry
through its 625 hops. The one-second terminal silence already clears the
384-sample DSP history before the boundary. A future transport source can
keep the epoch constant for the lifetime of a continuous input stream.
The sink accounts for the 384-sample output alignment and trims 250 ms at
each section edge. Every case therefore summarizes 8,000 samples.
It reports input, output, residual-to-clean and clean RMS. It checks finite
output using float bit patterns, including when fast-math is enabled.

Cycle checks require silence output below 1e-5 full scale, echo-only RMS
below 95% of its input, double-talk residual below half the input residual,
and near-only output RMS between 40% and 160% of the clean signal. These
thresholds test this synthetic demonstration. The sink retains failed-check
counts and keeps streaming.

## Host results

Run from the repository root in an MSVC developer environment or supply
the developer script explicitly.

```powershell
.venv/Scripts/python.exe tests/run_dtln_tests.py --vcvars "<Visual Studio>/Common7/Tools/VsDevCmd.bat"
```

The test compiles the actual generated scheduler and wrappers with MSVC
and fast floating-point optimization. It ran 628 consecutive hops, checked
the independent model states, the exact reference time window at every
stage2 invocation, and the 384-sample alignment. Unity spectral masks and
a quarter-gain stage2 block reconstructed the input after four-way
overlap-add. Case RMS differences were below 1e-6. A destructive FFT shim
verified that the reference time branch survives FFT input mutation.
The test also passed model-NaN rejection, late-source scheduling,
absolute-delay expiry recovery and genuine delay-error propagation. It
verifies both recurrent states reset when the source crosses into episode 2.

Graph generation succeeded from the repository root and project folder.
The fake pattern generator's independent C++/Python parity check passed
within 2.99e-8 full scale over two repeated periods.

## Board results

The final ReleaseSRAM image used 606,224 bytes of MRAM and 2,556,400 of
4,194,304 SRAM0 bytes. It checked PTE CRC `0x5AE153B6` before and after
copying to SRAM0. Both independent methods passed with zero maximum
output and state error against the integer TOSA fixtures.

At the debugger snapshot after 9,640 complete hops, both stages and the
sink had processed 9,640 blocks. The sink had emitted 1,233,920 samples
and passed 15 complete case suites. The maximum processing interval was
825,263 ticks at 400 MHz, or 2.0631575 ms. Deadline misses, late source
starts, nonfinite samples, case failures and pipeline error were all zero.
The UART trace continued through 17 passing case suites.
[Final UART capture](../model/benchmark_uart_sram_release.txt)

| Synthetic case | Mixed input RMS | Enhanced RMS | RMS error against clean |
| --- | ---: | ---: | ---: |
| Opening silence | 0 | 0 | 0 |
| Echo only | 0.048997 | 0.002547 | 0.002547 |
| Double talk | 0.091159 | 0.078771 | 0.010634 |
| Near-end only | 0.076706 | 0.072008 | 0.014483 |
| Closing silence | 0 | 0 | 0 |

Before adding the test episode boundary, optimized build `b-9` ran
the repeated stimulus with uninterrupted recurrent state. Timing still
met the deadline. Near-end-only output RMS dropped to 0.002720 on the
fourth repeat and remained low. The upstream float TFLite run with
XNNPACK kept near-end RMS between 0.063431 and 0.067646 across six repeats.
The host pre-Vela quantized graph also showed accumulating attenuation,
from 0.070088 on the first repeat to 0.025985 on the sixth. The different
onset on the board shows that single-fixture parity does not predict the
whole recurrent trajectory.
The board result establishes deterministic independent test episodes.
Long continuous recurrence with the integer model needs separate
validation before a streamed-audio deployment.
[Continuous-state UART capture](../model/benchmark_uart_sram_continuous.txt)

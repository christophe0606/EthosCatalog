# Streaming demo validation

ReleaseSRAM build `b-11` and load `d-12` passed on Alif DevKit-E8.
This report records the synthetic acceptance cases, host scheduler test,
and final board evidence.

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
[Final UART capture](model/benchmark_uart_sram_release.txt)

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
[Continuous-state UART capture](model/benchmark_uart_sram_continuous.txt)

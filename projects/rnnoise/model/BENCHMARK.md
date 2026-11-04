# RNNoise board benchmark

Two Release configurations measured on Alif DevKit-E8, Cortex-M55 HP and Ethos-U85
256 MAC/cycle. AC6 6.24.0 uses `-O3 -ffast-math`; ExecuTorch 1.4.1, RTX5 5.9.1.
CPU, AXI/NPU and RTOS timer clocks each verified **400 MHz**. Two warmups precede
ten checked fixture forwards.

| Measurement | CPU baseline | Optimized NPU |
|---|---:|---:|
| Build / load-debug jobs | b-11 / d-12 | b-13 / d-14 |
| PTE bytes | 168,040 | 217,048 |
| NPU invocations per forward | 7 | 2 |
| Dense operators on NPU | 0 / 9 | 9 / 9 |
| Whole-forward median (ms) | 35.0491575 | 0.38663375 |
| Whole-forward min / max (ms) | 35.038650 / 35.125485 | 0.3858925 / 0.3881275 |
| NPU median cycles / ms | 3550.5 / .00887625 | 60847.5 / 0.15211875 |
| NPU cycles min / max / mean | 3459 / 3597 / 3544.8 | 60663 / 60966 / 60838.0 |
| Method / temp peak bytes | 392,968 / 864 | 3,824 / 928 |
| MRAM / SRAM0 bytes | 643,568 / 786,448 | 603,216 / 786,448 |
| 10 ms inference budget | Exceeded | Met |

Whole-forward speedup is 90.65 times. Measured maximum leaves
9.611873 ms per frame for DSP/capture/display. This is inference
headroom; full microphone/DSP/LCD timing is still unmeasured. Vela reports
24,684 + 62,994 = 87,678 MACs including elementwise work (86,952 dense MACs).

Timer wraps `module.forward`, including CPU arithmetic/QDQ, both delegates and
PMU hooks. Adapter tensor/vector setup, method-load check, output copy,
validation, capture, DSP, LCD and UART are excluded. Model initialization is
outside timing. YAML `time.ms` is NPU time; `time.forward_ms` is whole forward.
No end-to-end audio latency is claimed.

## Numerical checks

Outputs: denoise state96, gains22, noise state48, VAD state24, voice probability1.
Fixture timing reloads original fixture states. A separate160-frame synthetic
silence/harmonic-plus-noise/silence replay resets once (VAD/denoise -1, noise
-128) and feeds board states back independently. Every output is compared with
separately recurrent host and original TFLite references.

| Output | Fixture max vs host LSB | Fixture max vs TFLite LSB | Sequence max vs host LSB | Sequence max vs TFLite LSB | Sequence mean vs TFLite LSB |
|---|---:|---:|---:|---:|---:|
| Denoise state | 0 | 25 | 0 | 103 | 7.37516737 |
| Gains | 0 | 8 | 0 | 37 | 6.13353682 |
| Noise state | 0 | 2 | 0 | 10 | .723317027 |
| VAD state | 0 | 0 | 0 | 23 | 2.03960919 |
| Voice probability | 0 | 0 | 0 | 20 | 1.47500026 |

All five sequence mean errors vs host are zero. Baseline and optimized board
both exactly match imported host; NPU delegation introduced no observed error.
Source TFLite errors remain from import/rounding and recurrent accumulation:
gain MAE **.0239590704**, max37/256=.14453125; voice probability MAE
**.00576171977**, max20/256=.078125. Denoise state drift reaches103LSB.
Synthetic checks do not establish clean-speech preservation or speech quality.

Before optimized results, acceptance was declared as runtime/PMU success,
separate fixture-vs-host provisional8LSB parity flag, and DEMO source-sequence
smoke gate gainMAE<=.05 and VADMAE<=.03. All flags pass; no tolerance changed.
Runtime/load/forward errors0; completion/operational/parity/numerical flags1.
This is a demo smoke gate, not speech-quality acceptance.

Each timed forward verified two nonzero NPU invocations, cycles=active and no
overflow. Final aggregate includes last timed forward plus160sequence calls:
322invocations, cycles=active=9,832,065, overflow0; not the per-call timing.

## Memory and provenance

Pools remain512KiB method/256KiB temp in SRAM0; optimized peaks3824/928B. No
resizing in this run. Baseline128KiB method pool failed ExecuTorch0x21 because
planned buffer needed353,264B; project pool change fixed it. Optimized build
MRAM603,216B, SRAM0786,448B, DTCM RW134,292B plus separately reserved RTX regions,
heaps/stacks; see full build-artefact report for those regions.

| SHA-256 | CPU baseline | Optimized NPU |
|---|---|---|
| PTE | ced0773de436f527349a4a58bc849710e0e2cb9ef8ab27d1e76d9919be2b58f6 | fdb9f9290446b7bc0d6a450747d38e248aa2065035dab99ab8501c5d4e6e5106 |
| Firmware ELF | 82e0e53f511875b61b551ed3f1fd0bf391387e0d59fb95faef8da997ac42ea08 | 06ef64187ab7594cb3cb47cfe5e0c460669225ed71d2147f54ab1cebdd7b3e0c |
| Imported graph | d7ad2f521700a89e3d7e13f3017fdedb2607b60b1366c5e4c9e495c485cf1bc7 | f39585491650368b1a51440bab9ac6ab565c12f2fdea2e637eb869a5de00dfc0 |
| Safetensors | cd5627ea7cf9a63a35e349f7d208301cc1b0b11b145434986f5a674978288938 | 2cb97a4299bb2140e5796ccee3a0699e961e9916ea0226822d652f342b3fbcb0 |
| Network config | 61e65ab395bd2d7fec76e48c034c30e15a9a73ef7541bba8afe2a622a9173b50 | 61e65ab395bd2d7fec76e48c034c30e15a9a73ef7541bba8afe2a622a9173b50 |

Baseline preserved in `benchmark_cpu.yml`, `BENCHMARK_CPU.md`,
`benchmark_cpu_debug.txt`, `benchmark_cpu_build_memory.txt` and frozen
`.flow/rnnoise/baseline_int8_cpu/` model/config/import artifacts. Use `_cpu`
captures for baseline (original report predates filename suffix). Optimized
capture/report are `benchmark_debug.txt` / `benchmark_build_memory.txt`.
`benchmark.yml` retains both runs and passes repository schema.

User-authorized clean rebuild and fresh session resolved original stale YAMNet
symbols. Optimized build/load uses matching
`out/rnnoise/DevKit-E8/Release/rnnoise.axf`. No UART text claimed; results are
typed debugger globals. Debugger expression fallback attempts emitted DAP
diagnostics despite successful typed results; final session responsive/running,
zero tool-level errors/timeouts. No pack edits or computer UI operations.
Target resumed and left running, serial ports closed.

---
description: Real-time DTLN-aec 128 echo cancellation on Alif DevKit-E8 with synthetic audio and UART metrics.
category: audio
catalog: true
---

# DTLN-aec on DevKit-E8

This port uses the pretrained 128-unit DTLN-aec model and an ExecuTorch
program with two Ethos-U85 methods. The fake source supplies a synchronized
microphone mixture and far-end reference. The fake sink checks the enhanced
samples and reports timing and signal levels. The demo uses the
board console for results. Its audio endpoints exchange blocks of 128 float
samples so a future Ethernet implementation can use the same interfaces.

The signal path processes mono 16 kHz audio using 512-sample windows and
128-sample hops. Each hop has an 8 ms processing budget. Windowing and
overlap-add introduce 384 samples (24 ms) of output alignment.

The CMSIS Stream graph exposes the rolling windows, FFTs, two PTE methods,
spectral mask and inverse FFT, and overlap-add. The fake source paces
the generated signal and the fake sink measures each complete hop.
Window history, recurrent state and overlap-add storage are bounded.
[Application graph](generated/app.pdf)

## Board results

The verified configuration is `DevKit-E8@DTLN_AEC_ReleaseSRAM`. It checks
the OSPI PTE and copies it to SRAM0 at startup. Guarded CPU operator kernels
from MIDI-DDSP reduce time spent in ExecuTorch's float normalization and
quantization. Median full-adapter timings were 0.667 ms for stage 1 and
1.072 ms for stage 2. Both startup methods matched their independent
integer TOSA fixtures exactly.

The complete processing path took at most 2.063 ms across 9,640 measured
hops. The UART trace recorded 17 consecutive passing test episodes with
zero deadline misses. Echo-only RMS fell from 0.048997 to 0.002547.
The fake source starts each five-second episode with fresh recurrent state
at its silent replay boundary. The continuously repeated synthetic stimulus
exposed near-end attenuation after four cycles without that boundary.
The current port verifies the scripted episodes and leaves continuous-stream
fidelity for the future transport integration. [Board validation](VALIDATION.md)

## Host results

The 1,763,792-byte PTE contains one Ethos delegate for the spectral mask
method and two for the time-domain enhancement method. The quantized host
pipeline reduced echo-only RMS from 0.0490 to 0.0176 on the five-second
synthetic pattern. Its mean absolute waveform error against the original
TFLite pipeline was 0.00263 of full scale. Details include the quantization
recipe, recurrent-state differences, and reproducible host checks.
[Conversion report](model/CONVERSION.md)

The generated scheduler completed 628 hops in the host integration test.
It exercised the production Stream nodes with FFT and model test doubles
to check reference-branch integrity, independent recurrent states, 384-sample
output alignment, and pace recovery after a missed deadline.
[Stream validation](VALIDATION.md)

Regenerate the graph and run the host conversion check from the repository root.

```powershell
.venv/Scripts/python.exe projects/dtln_aec/app.py
.venv/Scripts/python.exe projects/dtln_aec/model/validate_conversion.py
```

## Board procedure

Use `DevKit-E8@DTLN_AEC_ProgramModel` to prepare the flash controller and
program the PTE at `0xC3C00000` with the model-only run file. The Release
targets share the programmed image. `DevKit-E8@DTLN_AEC_Release` reads it
directly from OSPI. `DevKit-E8@DTLN_AEC_ReleaseSRAM` copies the PTE to SRAM0
once at startup. Both verify the PTE CRC and run the independent method
fixtures before starting the synthetic demo. [OSPI programming](../../documentation/alif-e8-flashing.md#program-an-ospi-model)

Use ReleaseSRAM for the paced demo. The initial direct-OSPI stage methods
took 62.76 ms combined from their individual adapter medians.
[Benchmark results](model/BENCHMARK.md)

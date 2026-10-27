---
description: Experimental FARGAN text-to-speech benchmark; numerical and real-time checks fail on DevKit-E8.
category: speech
catalog: false
---

# rvTTS FARGAN experimental benchmark

This separate CMSIS cproject runs the converted rvTTS prosody, acoustic and
FARGAN vocoder models on Alif DevKit-E8. It is an **experimental, non-real-time
benchmark**, not an accepted UART/I2S/LCD speech demo.

The Release board benchmark establishes:

- Six Ethos-U85 delegates execute and memory fits.
- A complete vocoder frame costs 108.258 ms for 10 ms of audio.
- An 81-frame prosody-to-PCM schedule costs 9,562.321 ms for 820 ms of output
  (real-time factor 11.661).
- Both vocoder methods fail the strict quantized-host reference checks.
  Tolerances remain unchanged. The four shared methods pass.
- The 81-frame board waveform comparison has 5.289 dB synthetic SNR versus its
  float reference. The earlier 173-frame host PTQ result is a different test
  at 13.109 dB. Neither establishes audible speech quality.

See [CHECKPOINT.md](CHECKPOINT.md) and
[model/BENCHMARK_REPORT.md](model/BENCHMARK_REPORT.md) for measurements, memory,
numerical diagnosis and exact limitations. The measured benchmark is
[model/benchmark.yml](model/benchmark.yml); raw UART and per-method records are
stored beside it. All benchmark YAML files pass the repository schema.

The target sets are `DevKit-E8@RvTTS_FARGAN_Debug`,
`DevKit-E8@RvTTS_FARGAN_Release` and `DevKit-E8@RvTTS_FARGAN_ProgramModel`.
Select the intended set and use CMSIS Developer Assistant for building,
programming, running, debugging and serial. Release uses `-O3 -ffast-math`.
The generated [ai_layer](ai_layer/ai_layer.clayer.yml) belongs to this project.
Follow the repository [OSPI programming procedure](../../documentation/alif-e8-flashing.md)
for the PTE and source assets. Both TTS projects use the model partition
starting at `0xC0000000`, so switching firmware also requires programming the
selected variant's model when the bytes differ.

The benchmark starts at boot, tests held-out method fixtures, generates complete
synthetic utterance PCM into SRAM, prints samples and a final FAIL over UART4,
then remains running in an idle RTOS thread. It does not play that PCM through
I2S or animate it on LCD. The working LPC listening demo is the first-phase
demonstration; no FARGAN speed or quality optimization follows these failures
in this phase.

Source/provenance and reconstruction are documented in
[model/CONVERSION_REPORT.md](model/CONVERSION_REPORT.md) and
[model/ADAPTER_CONTRACT.md](model/ADAPTER_CONTRACT.md). Original Apache-2.0 and
BSD-3-Clause notices are preserved in `model/upstream/`.

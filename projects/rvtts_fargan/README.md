---
description: Hybrid UART text-to-speech with Ethos-U85 prosody/acoustic inference and a Helium CPU FARGAN vocoder.
category: speech
catalog: true
---

# Hybrid FARGAN UART speech demo

This separate cproject keeps the prosody and acoustic networks on Ethos-U85 and
runs the FARGAN vocoder on Cortex-M55. Its four-method ExecuTorch layer belongs
to this project. CPU weights are copied once from OSPI into SRAM0 and DTCM and
processed with a CMSIS-DSP maximum reduction and Helium int16 matrix kernels.

The packed Helium kernel and DTCM weight placement pass the on-board
173-frame comparison against the original float C vocoder: 51.955 dB SNR,
0.0000636759 normalized PCM RMSE and 0.000597819 maximum difference.
The shared neural PTE is copied once from OSPI into SRAM0 and CRC-checked;
the four Ethos methods then read command streams and weights through SRAM ports.

Release b-53 / r-57 meets real time on all three measured phrases:

| Text | Audio | Complete synthesis | RTF |
|---|---:|---:|---:|
| Hello. | 930 ms | 771.137 ms | 0.8292 |
| One two three. | 1230 ms | 1014.411 ms | 0.8247 |
| This is a working demo. | 1730 ms | 1421.190 ms | 0.8215 |

CPU frame processing is 7.354-7.390 ms per 10 ms audio frame. All 389 audio
blocks complete without underruns, missed deadlines or UART/pipeline errors;
the landscape LCD animates playback. Actual SRAM0 usage is 3,959,684 /
4,194,304 bytes, with 764,164 / 884,736 bytes ordinary DTCM.
Synthesis uses whole-utterance buffering: first audio starts after 0.879 /
1.119 / 1.528 seconds for these phrases. The user completed the FARGAN speaker
check on 2026-10-27 and reported: "FARGAN sounds much better". The demo is
accepted and included in the catalog.
See [CPU conversion](model/CPU_CONVERSION_REPORT.md),
[board results](model/cpu_board_validation.json) and
[raw UART](model/cpu_board_uart.txt).

Select `DevKit-E8@RvTTS_FARGAN_Release`. Build, program, run and use serial through
CMSIS Developer Assistant. Follow the repository
[OSPI procedure](../../documentation/alif-e8-flashing.md) to program its own PTE
and assets, including the packed CPU weight payload at `0xC1000000` (1,661,600
bytes, CRC32 `54ca3f64`). The shared model
partition starts at `0xC0000000` as in LPC; program the selected variant's assets
when changing projects.

Connect a powered speaker or suitable amplifier to the green **Audio Out** jack.
UART4 uses the PRG USB connection at 115200 baud, 8 data bits, no parity, 1 stop
bit. Firmware echoes typed characters; keep terminal local echo disabled.
Enter a short English sentence, then CR or LF. Press **Up arrow** to recall the
last submitted sentence; edit it if needed, then press Enter to speak it again.
Recall replaces any text currently being edited. Backspace/Delete erase the
previous character; CRLF submits once and echoes one newline. Periodic status
output pauses while a line is being edited.

Input limits are 160 printable ASCII characters and 224 frontend feature frames.
Long utterances are rejected explicitly. Submit one sentence at a time and wait
for playback to finish. Source FARGAN warmup, initial 80-sample delay removal and
final flush produce `(feature_frames+1)*160` samples at 16 kHz. The bounded PCM
buffer holds all 225 possible audio blocks. The landscape LCD plots those same
queued audio blocks; playback runs independently at realtime priority.

`app.py` generates the CMSIS Stream graph from the root or project directory.
The request/acknowledgment bridge permits one inference request at a time.
Separate nodes represent prosody pre/GRU/head, acoustic inference, pitch
normalization and CPU FARGAN. Inference and whole-utterance synthesis run on the
event thread, while UART input, I2S delivery and waveform publication remain in
the dataflow. The existing frontend/dictionary/vocabulary implementation is
shared with `../rvtts_lpc/` through repository-relative source references.

The old six-method **NPU FARGAN** trial failed real time (108.258 ms per 10 ms
frame) and strict numerical checks. Its original measurements remain in
[model/BENCHMARK_REPORT.md](model/BENCHMARK_REPORT.md), `model/benchmark_*.yml`
and raw UART/memory reports. Source snapshots and its project configuration are
in `npu_benchmark/`; `model/npu_network.yml` preserves its model configuration.
Those results describe the historical NPU vocoder, not the current CPU path.
The original catalog benchmark is preserved in `model/benchmark_npu_81.yml`;
`model/benchmark.yml` records the accepted hybrid demo.
Original Apache and BSD notices are preserved; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

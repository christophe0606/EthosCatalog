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

The earlier buffered Release b-53 / r-57 measured this synthesis throughput:

| Text | Audio | Complete synthesis | RTF |
|---|---:|---:|---:|
| Hello. | 930 ms | 771.137 ms | 0.8292 |
| One two three. | 1230 ms | 1014.411 ms | 0.8247 |
| This is a working demo. | 1730 ms | 1421.190 ms | 0.8215 |

CPU frame processing takes approximately 7.4 ms per 10 ms audio frame.
The current demo overlaps vocoder generation and playback through the existing
CMSIS Stream event and dataflow threads. It uses a four-block PCM queue, a
20 ms initial reserve and a two-block I2S queue; it never stores the complete
waveform before playback. Sentence-wide feature inference and pitch statistics
still run before the first PCM block.

First-audio latency on the streaming build is 128 / 149 / 189 ms for the three
phrases above, compared with 879 / 1119 / 1528 ms on the earlier buffered build.
See [streaming design and validation](../../documentation/rvtts-streaming.md)
and [streaming board results](model/streaming_validation.json).
The user accepted the original FARGAN sound on 2026-10-27: "FARGAN sounds much better".
The streamed `Hello.` PCM checksum matches the original buffered firmware.
Historical synthesis measurements and numerical validation remain in
[CPU conversion](model/CPU_CONVERSION_REPORT.md),
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
final flush produce `(feature_frames+1)*160` samples at 16 kHz. PCM storage is
bounded to four queued audio blocks plus a single block assembler, independent
of sentence length. The landscape LCD plots the blocks
being delivered; playback runs independently at realtime priority.

`app.py` generates the CMSIS Stream graph from the root or project directory.
The request/acknowledgment bridge permits one inference request at a time.
Separate nodes represent prosody pre/GRU/head, acoustic inference, pitch
normalization and CPU FARGAN. Inference runs on the event thread; a self-connected
step event generates one vocoder frame per handler, yielding between frames and
retrying after one RTOS tick when the PCM queue is full. UART input, I2S delivery and
waveform publication remain in the dataflow. The existing frontend/dictionary/
vocabulary implementation is shared with `../rvtts_lpc/` through
repository-relative source references.

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

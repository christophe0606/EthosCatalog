---
description: Real-time UART text-to-speech on Ethos-U85 with I2S playback and an animated landscape waveform.
category: speech
---

# LPC UART speech demo

Select `DevKit-E8@RvTTS_LPC_Release` and build/load through CMSIS Developer Assistant.
The four-method PTE, source embedding tables, PDK2 pronunciation dictionary and
packed word vocabulary reside in OSPI1. `model/network.yml` records their
non-overlapping addresses; `ai_layer/generation.json` records sizes and hashes.
Startup copies the 528,144-byte PTE to SRAM0, verifies its CRC and runs the
four neural methods from SRAM. The stored PTE and its quantization are unchanged.

Connect a powered speaker or suitable amplifier to the green **Audio Out** jack.
The WM8904 headphone output does not directly drive a passive loudspeaker.
UART4 is the PRG USB connection: 115200 baud, 8 data bits, no parity, 1 stop bit.
The firmware echoes typed characters, so leave terminal local echo disabled.
Enter a short English sentence, then CR or LF. Press **Up arrow** to recall the
last submitted sentence; edit it if needed, then press Enter to speak it again.
Recall replaces any text currently being edited. Backspace/Delete erase the previous
character on the terminal; CRLF submits once and echoes one new line. Examples:
`Hello.` and `One two three.`.

The frontend accepts at most 160 printable ASCII characters. The stricter neural
context limit is 224 frontend frames (2.24 s); pronunciation and punctuation determine
how much text fits. Long input is explicitly rejected with its frame count and
must be shortened. It is never truncated or synthesized as independent GRU chunks.
For example `The meter is reading forty five.` produces 227 frames and is rejected.
Submit one sentence at a time, waiting for playback to finish. One latest pending
line can be retained while busy; rapid multiple lines are not a command queue.

`app.py` generates the CMSIS Stream graph from the repository root or this folder.
UART dataflow feeds a one-request/ack bridge. An event thread runs text processing,
then separate prosody pre/GRU/head/acoustic nodes, whole-utterance pitch correction
and the LPC vocoder. Playback dataflow remains at realtime priority, independent of
inference. The I2S3 interrupt maintains an eight-block stereo queue at 16 kHz with
32-bit slots, duplicating 16-bit mono speech into the high 16 bits of each channel.
The landscape LCD shows the queued waveform at 20 updates/s and playback progress.
LCD updates are coalesced while inference occupies the event thread; these skips
are counted separately from dropped events.

WM8904 configuration uses the board's 12.288 MHz oscillator, SYSCLK/fs=768 and
16 kHz sample-rate selection (WM8904 Rev 4.1, p101 Table 62). Clock and interface
registers are read back at startup. The selected pack driver supplies the DAC,
charge-pump and headphone power sequence. Touch is not initialized, leaving I2C1
with the codec callback. Shared LCD driver operations are reused unchanged.

Builds retain the original four independent method fixtures as startup tests.
`validate_runtime.py` additionally checks all 124,912 pronunciation entries against
an independent blob decoder, all 14,681 packed vocabulary lookups,
frontend bounds and deterministic LPC PCM on a host clang compiler. See
`runtime_validation.json` and `LIVE_VALIDATION.md` for evidence and limitations.
The original feature-only benchmark remains in `model/BENCHMARK_REPORT.md`; its
saved captures and generated fixtures are unchanged. `benchmark_main.cpp` retains
the benchmark entry point but is excluded from the demo build.

The supplied PDK2 lookup scanned front-coded bucket headings from the start for
each binary-search probe. This cost 1.67 s of frontend work for `One two three.`.
The project-owned `nodes_impl/pron_dictionary.c` instead probes self-contained
bucket entries through the existing offset index; host validation confirms every
pronunciation and stress value. This correction is required to meet real time.

The user completed the speaker listening check on 2026-10-27 and reported that
the sound is OK, then authorized CMSIS-DSP and Helium optimization. The LPC
vocoder now uses sparse pulse-dispersion overlap-add and bounded CMSIS-DSP
spectral dot products, with a source comparison on the target before startup.
The imported scalar implementation remains unchanged. See `dsp/README.md` for
the numerical contract and `LIVE_VALIDATION.md` for board evidence.

With the optimized CPU vocoder and SRAM model cache, Release synthesis takes
89.8 / 119.7 / 170.3 ms for the three validated phrases, versus 920 / 1220 /
1720 ms of audio (RTF 0.0976-0.0990). All 386 I2S blocks complete without
underruns, deadline misses or UART/pipeline errors. See
`dsp/board_sram_validation.json`; SRAM0 usage is 3,331,108 / 4,194,304 bytes.

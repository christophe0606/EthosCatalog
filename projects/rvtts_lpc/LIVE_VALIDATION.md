# LPC demo board validation

2026-10-27, DevKit-E8 M55_HP, Arm Compiler 6.24 Release. CMSIS build **b-22**
was loaded and run as **r-23**. The following samples were collected without a
debugger halt or concurrent host compilation. The target remains running.
Raw UART evidence is in `validation_uart.txt`; machine-readable results and
firmware/PTE hashes are in `demo_validation.json`.

| UART text | Frames | Audio | Complete synthesis | RTF | First I2S audio |
|---|---:|---:|---:|---:|---:|
| Hello. |92|920 ms|566.339 ms|0.6156|669.978 ms|
| One two three. |122|1220 ms|752.057 ms|0.6164|849.976 ms|
| This is a working demo. |172|1720 ms|1088.708 ms|0.6330|1189.975 ms|
| Hello. (UART backspace/CRLF) |92|920 ms|567.304 ms|0.6166|669.979 ms|

Complete synthesis starts before text normalization and dictionary lookup and
ends after pitch correction and LPC PCM generation. It includes event-thread
scheduling, intervening LCD work, tensor construction, four neural stages and
output copies. The caller's serial typing/transmission and subsequent playback
are outside that interval. First-audio timing starts at the same point and ends
when the first speech block is handed to I2S3; the hardware FIFO adds less than
one block of uncertainty. It includes the existing silence in the playback queue.

All 478 speech blocks completed through the **I2S interrupt** path (DMA is disabled):
76,480 mono samples, duplicated to both channels. At 16 kHz this is 4.780 s of audio.
The 400 MHz system timer measured callback intervals of 3,999,593–4,000,369 ticks,
or 9.99898–10.00092 ms per 160-sample block. The counters stayed at zero for audio
underruns, intervals above 11 ms, Stream event drops, UART ring drops/errors and
pipeline errors. The sample-count evidence establishes delivery to the I2S FIFO,
not an acoustic recording of the speaker.

LCD presentation advanced from 92 to 3266 frames in the captured run, approximately
20 fps while idle and during playback. The renderer uses logical800×480landscape
coordinates, rotates them into the native480×800RGB565 framebuffers, and plots
the same PCM blocks queued to the speaker. The queue can make the waveform lead
physical playback by about 80 ms. Forty-seven LCD updates were intentionally
coalesced while inference occupied the event thread; no audio block was dropped.

UART checks passed: `HelloX`, Backspace, `.`, CRLF submitted exactly one `Hello.`
utterance. A 161-character line reported `REJECT line exceeds160 ASCII characters`
without starting inference. `The meter is reading forty five.` reported 227 frames
and was rejected against the 224-frame bound. Both rejection paths returned to
ready state; the final capture shows 6 requests and 6 acknowledgments, 478 completed
speech blocks and error=0. The demo does not claim a gapless stream of multiple
utterances; whole-utterance buffering preserves bidirectional context.

Startup `self-test PASS` is reached only after PTE/source/dictionary/vocabulary
CRC checks, initialization of all four persistent neural methods, their independent
numerical fixtures, codec power sequencing, WM8904 clock/interface register
readback (0x15=0x1802,0x19=0x000e), I2S3 initialization and LCD initialization.
Expected CRC32s are PTE 7e22fd20, prosody 8508b99f, acoustic f3abbe96,
dictionary 621e137e and vocabulary 96c08512. Board schematic 220-00319B, p7 gives
the codec 12.288 MHz oscillator; WM8904 Rev 4.1, p101 Table 62 specifies the768×
SYSCLK ratio for16 kHz. The driver supplies headphone/DAC power sequencing.

`validation_memory.txt` is the CMSIS build-artifact report for this exact image:
SRAM0 uses 2,702,468/4,194,304 B, leaving 1,491,836 B. MRAM uses 638,992 B;
ordinary DTCM data uses 158,400 B, with the separate configured heap/stack regions
listed in that report. The 1,536,000 B pair of LCD framebuffers and 107,480 B
frontend/features/PCM work object are included. No memory limitation blocks
catalog inclusion. OSPI payloads total 2,704,410 B before flash alignment.

The only processing acceleration in this phase corrects the supplied dictionary
decoder's repeated linear heading scans. Its project-owned replacement uses the
existing direct bucket index. `validate_runtime.py` checks all 124,912 pronunciations
and stress bytes against an independent blob decoder, all 14,681 vocabulary entries,
frontend bounds and deterministic 25,120-sample source LPC output (CRC 752c020c).
The original feature-only benchmark and its evidence remain unchanged in `model/`.

The firmware is ready for the user's external-speaker listening check. No audible
quality claim or optional Helium/node optimization is approved by these electrical,
numeric and timing checks.

## Final board handoff

After the separate FARGAN benchmark, the LPC model was restored to OSPI with the
project model-only flash image. The `RvTTS_LPC_Release` build b-32 succeeded and
`load_and_run` r-33 started the LPC demo. A fresh UART `Hello.` request produced
92 frames and 92 completed I2S blocks. Its live report showed `error=0` and
zero underruns, deadline misses, event drops and UART errors. The CMSIS Run task
remains active for the external-speaker listening check.

On 2026-10-27 the user completed the external-speaker listening check and
reported: "Sound is ok." This accepts the LPC demo's audible output. The
FARGAN variant has not received speaker validation.

## UART echo validation

Release build b-36 and load/run r-37 add firmware input echo. A single `H`
was returned before Enter. No status output arrived during a six-second pause
with the line partially entered. Completing the line with
`elloX`, Backspace, `Y`, Delete, `.`, CRLF returned both erase sequences as
`08 20 08` and exactly one `0d 0a` newline. The submitted text was `Hello.`
once (`requests=1`, `acks=1`), with 92 completed audio blocks and zero pipeline,
UART, underrun, deadline or event-drop errors. The serial port was released
and r-37 remains running for terminal use with local echo disabled.

## CMSIS-DSP and Helium optimization

After speaker acceptance, the user authorized CPU optimization. Release b-38
and clean load/run r-41 validate the project-owned optimized LPC vocoder.
The three target comparison fixtures each synthesize 157 frames. Median CPU
times decrease from 72.34-72.40 ms to 36.42-36.75 ms (1.97-1.99 times faster).
The actual MVE floating-point and CMSIS-DSP MVE paths are enabled. Maximum
PCM error against preserved scalar C is one count; SNR is 90.20-93.47 dB.

| UART text | Frames | Complete synthesis | RTF | LPC CPU vocoder |
|---|---:|---:|---:|---:|
| Hello. | 92 | 544.836 ms | 0.5922 | 21.441 ms |
| One two three. | 122 | 724.475 ms | 0.5938 | 28.421 ms |
| This is a working demo. | 172 | 1059.542 ms | 0.6160 | 40.035 ms |

Complete synthesis improves by 2.68-3.80 percent relative to the earlier
unoptimized demo; shared Ethos inference still dominates. All 386 I2S blocks
(61,760 samples / 3.86 seconds) complete with zero underruns, deadline misses,
event drops, UART errors or pipeline errors. Callback intervals are
9.99885-10.00124 ms. The landscape LCD continues updating. SRAM0 uses
2,802,948/4,194,304 bytes, including the startup reference comparison buffers.
`dsp/board_uart.txt` and `dsp/board_validation.json` preserve the measurements.

An initial hot reload r-39 failed during Ethos startup before the LPC checks.
Live inspection and a fresh debug load restored startup without a source
change. The reported run r-41 is a subsequent unhalted Release load/run with
all self-tests passing; failed-run timing is excluded.

## SRAM model cache: b-51 / r-52

The engine now copies the unchanged 528,144-byte PTE from OSPI into SRAM0
once, verifies the copied CRC (`7e22fd20`) and loads all four methods there.
Command streams and neural weights use U85 SRAM AXI ports. The existing
driver performs cache maintenance and barriers at NPU ownership transitions.
This storage optimization preserves the saved quantized graphs and fixtures.
Both project AI layers were regenerated.

All four neural fixtures and the three target LPC/reference comparisons pass.
Maximum LPC difference remains one PCM count, with SNR 90.198-93.473 dB.

| Text | Audio | Synthesis | RTF | Speedup over CPU optimization alone |
|---|---:|---:|---:|---:|
| Hello. | 920 ms | 89.775 ms | 0.09758 | 6.07x |
| One two three. | 1220 ms | 119.745 ms | 0.09815 | 6.05x |
| This is a working demo. | 1720 ms | 170.292 ms | 0.09901 | 6.22x |

Synthesis includes frontend, feature inference, pitch, vocoder and PCM
publication; subsequent playback is excluded from RTF. Playback still uses
whole-utterance buffering. All 386 blocks (61,760 samples) complete, with
zero underruns, deadline misses, event drops, UART errors and pipeline errors.
The 9.99919-10.00112 ms callback intervals meet the 10 ms block period.
The landscape LCD records 904 presentations and no coalesced updates.

Actual SRAM0 usage is 3,331,108 / 4,194,304 bytes, MRAM 673,680 bytes,
ordinary DTCM 158,412 bytes. Raw UART and calculations are in
`dsp/board_sram_uart.txt` and `dsp/board_sram_validation.json`.

## UART Up-arrow history: b-58 / r-59

Up arrow recalls the last nonempty, non-overflowed submission into the editable
line; Enter submits it. Both CSI (`ESC [ A`) and application cursor (`ESC O A`)
encodings work, including sequences split across UART reads. A recall replaces
partial input and preserves echo, Backspace/Delete and single CRLF submission.
Empty history is ignored; blank submissions preserve history. Other arrow
sequences are consumed without inserting their bytes into the sentence.

Board checks cover split CSI, SS3 replacement of partial text, editing the
recall, recalling the edited submission, repeated Up and parameterized CSI.
Recall alone does not synthesize, and periodic status remains quiet through
six seconds of editing. Four submissions complete 426 I2S blocks without
errors, underruns or missed deadlines. Startup neural/LPC checks pass.
See `uart_history_validation.json` and `uart_history_uart.txt`.

---
description: Live Silero voice activity detection on the microphone with Ethos-U85 and a retriggerable one-second landscape LCD fade.
category: audio
catalog: true
---

# Silero voice activity detector

The Alif DevKit-E8 PDM microphone supplies mono 16 kHz audio in 512-sample
(32 ms) blocks. The landscape LCD shows **VOICE DETECTED** for positive model
windows (probability >= 0.5). Each positive window resets the message to full
brightness and starts a linear one-second fade immediately. Further speech
restarts the fade; after one second without detection the message is invisible.
The title and listening prompt remain visible.

## Run

Select `DevKit-E8@SileroVAD_Release` in the CMSIS Solution view, build, then
Load & Debug. The startup checks run before live microphone capture begins.
Speak near the onboard microphone to trigger the message; stop speaking to see
it fade. `SileroVAD_Debug` is also available, but timing is measured in Release.

## Pipeline and scheduling

`app.py` defines MicrophoneSource -> PCMToFloat -> SileroInference ->
VADDisplayClock, with bounded snapshot events to VADLCD. Reusable capture and
normalization nodes come from `common`; the model context, recurrent state,
detection policy, and display animation stay in project-owned nodes.
The generated schedule has four dataflow firings and 3,072 bytes of FIFO storage.

SileroInference owns the 64 preceding PCM samples and two 128-value recurrent
states. Capture discontinuities reset both before processing the next block.
Each probability >= 0.5 refreshes the detection timestamp. VADDisplayClock
publishes the latest timestamp and detection generation at nominal 20 fps; a
failed event send retains the newest detection for the next publication.
The LCD node owns fade/backbuffer state and uses unsigned elapsed tick arithmetic,
including tick wrap during the one-second interval. Display rendering and VSYNC
run on the Stream event thread, independently of realtime audio inference.
Display refreshes are quantized to microphone blocks, so intervals alternate
between approximately 32 and 64 ms (nominal 20 fps).

Measured Release whole-forward time is 1.65-2.20 ms, median 1.92 ms, against the
32 ms audio interval; inference runs inline. The live inference counter measures
the full adapter including input setup and output copying. No utterance buffer
is accumulated. Microphone capture has a bounded hardware ring (at most seven
queued 32 ms blocks); the normal steady-state path processes each available block.

## Model, fidelity, and memory

Pinned MIT Silero weights are reconstructed in PyTorch, post-training quantized
with int16 activations/int8 weights, and automatically partitioned by ExecuTorch
into two Ethos-U85 delegates with CPU pow and quantize/dequantize remainder.
The 393,504-byte PTE is embedded in MRAM. Runtime pools reserve 65,536 method
bytes and 131,072 temporary bytes. RGB565 double buffering reserves 1,536,000
SRAM bytes. No imported library is modified and no codec dependency is needed.
See [conversion](model/CONVERSION.md), [benchmark](model/BENCHMARK.md), and
[benchmark.yml](model/benchmark.yml) for provenance, metrics and measured limits.

Float reconstruction matches upstream exactly on 937 calibration frames.
Pre-Vela PTQ agrees with float on 98.08% of 937 held-out detection decisions.
The board agrees with original float on 97.40% of a separate 192-block sanity
stream; its mean probability error is 0.0253 and maximum transition error is
0.4892. Recurrent state differs by up to 0.5511 from host PTQ on the warm speech
vector. These are small-demo fidelity measurements, not labelled-dataset VAD
accuracy or exact-state parity. Broader validation is needed for product use.

## Validation and diagnostics

The startup self-test checks silence and warm-state speech probabilities,
finite recurrent states, two actual NPU invocations per forward, and ten timing
samples. It also replays a 192-block continuous stream followed by silence,
checking bounded finite probabilities, >=95% host PTQ threshold agreement,
mean error <=0.05 and a final non-speech probability. Transition errors and
state drift are recorded, with no claim that the original state tolerance passed.
Pure fade tests cover start, midpoint, expiry, retriggering and tick wrap.

Inspect `silero_test_passed`, `silero_benchmark_complete`, `silero_fade_test_passed`,
`pipeline_error`, and `stream_stats` using the debugger. Stream diagnostics include
audio/inference progress, full-adapter and audio-pipeline maxima, deadline misses,
state resets, detections, fade restarts/expiries, LCD frames and event drops.
`microphone_drops` and `microphone_overflows` report capture errors.
Pausing a live target disturbs capture; use fresh running intervals for timing.

Release build b-12/load d-13 passed startup inference, continuous-stream and fade
checks. Before the first debugger pause, 765 audio blocks and 492 LCD frames
progressed with zero microphone drops/overflows, audio discontinuities, event
drops, or audio/LCD deadline misses. Full-adapter maximum was 2.241 ms and the
complete audio pipeline maximum was 2.263 ms over that approximately 24-second
interval. A second sample reached 3,583 inferences and 2,306 LCD frames with
zero deadlines/event drops. One capture overflow/discontinuity immediately after
the debugger pause reset the recurrent state and recovered; no further natural
gap appeared. The build reports SRAM0 1,732,624 bytes including buffers and pools,
and approximately 1,081,296 bytes of MRAM image. Final reviewed Release build b-16/load d-17 passed both startup tests. Before
the final inspection, 1,665 inferences and 1,071 LCD frames progressed with
28 positive windows, 20 displayed fade refreshes and three fade expiries.
Pipeline errors, audio/LCD deadline misses, display-event drops, microphone
drops and overflows were all zero. The user confirmed the spoken-microphone
and physical LCD behavior as working. Firmware was resumed and left running.

## Regenerate

From repository root in the configured Python environment:

```powershell
.venv/Scripts/python.exe projects/silero_vad/app.py
.venv/Scripts/python.exe scripts/create_ai_layer.py --project silero_vad
```

Graph generation also works from this project directory via `app.py`.
Regenerate the AI layer only when model/quantization/Vela/storage settings change.
The original MIT notice is preserved in `model/upstream/LICENSE`; source hashes
are in `model/upstream/provenance.json`.

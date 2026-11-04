---
description: Pretrained thirteen-instrument MIDI-DDSP music synthesis with Ethos-U85 inference and streaming CPU audio; monophonic, audio-only demo.
category: audio
catalog: true
---

# MIDI-DDSP on Alif DevKit-E8

This project runs the pretrained 13-instrument
[MIDI-DDSP](https://github.com/magenta/midi-ddsp) synthesis model, an extension of
[DDSP](https://github.com/magenta/ddsp), as an on-device MIDI music demo.

It retains the thirteen supported instrument identities:
violin, viola, cello, double bass, flute, oboe, clarinet, saxophone, bassoon,
trumpet, horn, trombone and tuba. The demonstration plays a monophonic piece
with all thirteen instruments in sequence, as requested after listening to the
first board output. The final demo is audio only; no LCD or animations are used.

The included 104-second score and final reverb tail passed an uninterrupted
Release run with all thirteen identities: no active queue starvation, audio or
neural deadline misses, clipped samples, nonfinite output or dropped requests.
The maximum overlapped neural window was 3.918 seconds for four seconds of
controls. The margin is small; this result establishes the included monophonic
demo rather than a real-time guarantee for arbitrary MIDI files. See the raw
[final Release run](model/stream_final_b91.json). Headphone gain is 12 dB above
the original setting, including the final requested additional 6 dB. Both codec
channels read back code49 (-8 dB); digital synthesis gain is unchanged.

An isolated neural sweep takes at most 2.859 seconds per four-second window.
The CPU uses CMSIS-DSP FFTs, interpolated sine/amplitude tables, a four-interval
reverb grouping, strict floating-point Helium phase calculation and a
state-preserving shortcut during muted rests. The neural schedule, synthesis
and playback overlap through a bounded 96 ms PCM queue. No complete song or
four-second PCM window is buffered.

The model image is stored in OSPI; the current optimized execution stages
method weights/constants into SRAM and caches CPU constants. The earlier direct
OSPI baseline follows the repository's
[verified configuration](../../documentation/ospi-in-place-inference.md).
Measurements use 100 MHz OSPI and 400 MHz CPU/AXI/U85. Project-owned
[normalization kernels](kernels/README.md), quantization operators and batched
preconditioning account for the neural improvements below. Batching sixteen
independent precondition frames preserves weights and quantization parameters,
with checked board errors unchanged from batch one. Independent float
TensorFlow/PyTorch comparisons pass for all thirteen instruments; deployed
harmonic and closed-loop numerical warnings are documented separately.

| Measured configuration | Full neural schedule | NPU portion |
| --- | ---: | ---: |
| Original portable CPU operators, direct OSPI | 262.621 s | 28.333 s |
| Project CPU operators, direct OSPI | 34.417 s | 28.333 s |
| Project CPU operators, staged SRAM weights/constants | 5.215 s | 1.256 s |
| Staged weights, table CRC, fast quantize/native dequantize | 4.518 s | 1.256 s |
| Same, with sixteen independent precondition rows per call | 2.859 s | 0.859 s |

Each row produces four seconds of controls for one instrument and four
note/rest regions. The first four rows make 4,013 method calls; the final row
makes 3,076. The first row includes initial small
method loads; later rows follow held-method warm-up. All include caller
packing and sampling. See [measured results](model/BENCHMARK.md) for all thirteen
identities, memory and numerical/startup limits. A fresh successful run observed
NVIC pending both before and after driver reset; project startup clears it
before enabling the line. This supports the source-based idle-IRQ explanation
for the earlier intermittent first-call failure. Startup/readback and source
hashes accompany the playback evidence.

The CPU synthesizer must deliver audio incrementally while inference/generation
overlap playback through a small bounded buffer. Rendering the entire MIDI file
to an intermediate PCM buffer is not the intended implementation.

`player/midi_file.cpp` is a heap-free Standard MIDI File format 0/1 reader. It
merges tempo maps across tracks and retains note, program, controller and pitch
bend events in bounded caller-owned storage. `player/voice_allocator.cpp` applies
a supplied voice cap, handles sustain and release tails, and steals release tails
before held notes. It retains note identities across stealing and program changes.
This code has host coverage. The initial firmware player uses
[`score_windows.*`](player/SCORE_WINDOWS.md) to admit one instrument per
four-second model window, retain MIDI state and enforce a bounded region count.
The included score exercises all thirteen identities under that policy.

`player/score_features.py` is a separate host oracle for contiguous note/rest
regions already quantized to control frames. `player/validate_features.py`
compares it with the preserved upstream preprocessing functions, including
expression clipping, rest position and the upstream 100-region position mask.
Those comparisons pass exactly; MIDI time rounding and block seams still need
firmware validation.

`dsp/reference_synth.py` is a host reference for harmonic synthesis, filtered
noise and the learned reverb operation. Its numerical checks against the pinned
upstream DDSP core pass; see [DSP validation](dsp/README.md). The bounded CPU
implementation passes host and target DSP probes. Playback uses a
[bounded SAI queue](player/AUDIO_OUTPUT.md); the accepted run records complete
runtime counters and the remaining timing margin.

The final score is `assets/all_instruments_aligned.mid`: an original melody
played by each of the thirteen instruments in sequence, in its ordinary
register. Each instrument has a four-second phrase and a four-second rest,
aligning program changes to model windows. The score lasts 104 seconds, followed
by the final reverb tail. It contains MIDI events, with no prerecorded audio or
SoundFont. Regenerate and embed it from the repository root with:

```text
python projects/midi_ddsp/player/make_sequential_demo.py
python projects/midi_ddsp/player/embed_midi.py projects/midi_ddsp/assets/all_instruments_aligned.mid
```

`embed_midi.py <score.mid>` can select another Standard MIDI File before building
the firmware. Its notes are admitted by the bounded one-instrument window
policy in [SCORE_WINDOWS.md](player/SCORE_WINDOWS.md). The earlier
`single_voice.mid` and `all_instruments.mid` are host/benchmark fixtures generated
by `player/make_demo_midi.py`; their program timing is not the final demo layout.

Run the host parser and allocation checks from the repository root with:

```text
python tests/run_midi_ddsp_tests.py --cxx <host-c++-compiler>
```

On Windows, `--vcvars <path-to-vcvars64.bat>` selects the installed MSVC host
compiler instead. The runner accepts compiler paths as arguments and does not
require a particular external installation directory.

Upstream source code is Apache-2.0. Model-asset terms and the minimal dependency
set are under review. The optional upstream FluidSynth fallback will not be used.

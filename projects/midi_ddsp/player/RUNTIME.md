# Generated music runtime

The default score is original monophonic music played by all thirteen checkpoint
identities in sequence. `assets/all_instruments_aligned.mid` gives each identity
an eight-second slot: four seconds of music and four seconds for its learned
reverb to decay. The firmware parses score bytes and synthesizes the output; no
reference PCM or expected probe sample is sent to the codec.

`app.py` generates the CMSIS Stream graph from either the repository root or the
project directory. Eight purple nodes each execute one method of the same PTE.
The white CPU schedule node prepares their inputs and consumes their results.
Requests execute on the normal-priority event thread, configured at
`osPriorityHigh`; the dataflow/audio thread uses `osPriorityRealtime`. A request
remains outstanding until its completion acknowledgment. Results and
acknowledgments are delivered synchronously on the existing event thread; the
next method request is queued asynchronously, so inference does not recurse.

The dataflow emits one 64-sample synthesis interval per 4 ms control frame.
Reverb combines four intervals into 256 samples and applies the complete signed
48,000-tap learned FIR. PCM is copied to SAI immediately in 256-sample blocks.
The six-slot queue holds at most 96 ms, starts after 32 ms, and reverb grouping
adds at most 12 ms of accumulation. Subsequent neural inference and playback
overlap. A completed score drains its exact reverb tail, rounds only the final
block with zeros, drains the queue and stops the codec. The firmware remains
running with finite completion counters available to the debugger.

`SendToNetworkRuntime.hpp` keeps score and control-handoff state in a private,
statically allocated CPU backend context. There is one retained 504,000-byte
control bank. While it plays, the next harmonic result remains in the existing
neural planned arena. The final old control pair copies its actual lookahead
before releasing the old bank; only then can the new result replace it. Reverb
coefficient/history banks belong to the project reverb node. The startup DSP
profiler borrows them before the graph starts. No second control bank or
whole-song audio buffer is allocated.

The MIDI frontend admits one instrument stream and at most sixteen note/rest
regions per four-second window. This bounds the expression-method work, rather
than counting active keys alone. Other instrument notes in a locked window are
rejected and counted. Same-instrument overlap uses last-note priority. Excess
regions replace a suffix with hard dry silence and are counted. Ordinary rests
also hard-mute the dry signal while synthesis state and the old reverb continue.
A change of identity resets the synthesis stream and explicitly steals the old
reverb tail; its history is never convolved with the new instrument's IR.

To embed another Standard MIDI File, run
`python projects/midi_ddsp/player/embed_midi.py path/to/score.mid`, then build and
load the `MIDI_DDSP_Release` target set through CMSIS. The input path is supplied
by the caller. The parser accepts format0/1, uses the complete tempo map and has
a fixed 1,024-event storage bound; malformed or oversized scores fail explicitly.
All frame times round globally to nearest4ms, ties upward. Controller pitch bend
uses a fixed two-semitone range; velocity, volume and expression scale the dry
signal. Output is mono16kHz, duplicated into the codec's two32-bit I2S slots.

Inspect `midi_demo`, `midi_admission`, `midi_audio_nonfinite`,
`midi_audio_clipped`, `midi_event_dropped`, `midi_network_requests`,
`midi_network_completions` and `devkit_e8_audio_stats` after an uninterrupted
finite run. A prior debugger halt can disturb peripheral audio and timing
counters. Build/host success alone is not a real-time playback acceptance.

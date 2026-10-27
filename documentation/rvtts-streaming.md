# RVTTS generation and playback

Both `rvtts_lpc` and `rvtts_fargan` generate PCM progressively while earlier
blocks play. The existing CMSIS Stream event thread performs inference and
vocoder work; the higher-priority dataflow thread delivers PCM to I2S and
publishes waveform snapshots. No additional thread or utterance-sized PCM
buffer is used. The graph's vocoder step event loops back to the same node,
processing one frame per handler so other event handlers can run between frames.

The text frontend, bidirectional prosody network, acoustic feature extraction
and sentence-wide pitch normalization still complete before vocoder streaming
starts. This preserves the model's context and the existing pitch statistics.
It is not incremental synthesis from partially entered text.

## Bounded audio storage

Every complete mono block contains 160 PCM16 samples: 320 bytes / 10 ms at
16 kHz. The shared single-producer/single-consumer queue holds **four blocks**
(1280 bytes / 40 ms). Release/acquire atomics publish complete slots and prevent
reuse until the consumer has copied them. Playback starts with a **two-block
reserve** (20 ms), or the entire output if shorter. This reserve is part of
the four-block queue, not an additional queue.

The I2S ring holds **two blocks** (20 ms), reduced from eight. A further block
can be in the current dataflow iteration. Thus complete queued audio is bounded
to at most **70 ms**, regardless of sentence duration. FARGAN also retains one
160-sample assembly block; only an 80-sample / 5 ms partial block remains between
frames because of its original initial delay removal and final flush. Local
frame scratch space is fixed size. There is no complete waveform in `Work`.

When the PCM queue is full, the producer retains its frame/state, waits one RTOS
tick and posts another step event. It never overwrites unread PCM or waits on an
acknowledgment that needs the same event handler to return. The dataflow thread
continues to drain audio independently. LCD events remain coalesced and can run
between vocoder steps. Utterance acknowledgment occurs only after the final PCM
block has been copied out of the shared queue, so the next request cannot reset
state that is still in use.

## Timing and validation

`LIVE first_ticks` measures frontend start to the first speech block handed to
I2S. It does not measure analog speaker onset or terminal transport time.
`READY done` / `LIVE done` measures frontend start to final PCM publication.
For a sufficiently long utterance, `first_ticks < done` establishes that
playback begins before generation finishes. `vocoder` sums active frame/init
processing intervals, excluding queue backpressure and other event handlers.
`total` includes preprocessing through vocoder entry plus active vocoder work;
it must not be interpreted as streaming wall time or first-audio latency.

`queue` records the software queue high-water mark (maximum four), and `waits`
counts backpressure retries. `starves` records a missing PCM block after speech
has started; hardware underruns and missed callback deadlines remain separate
counters. Producer and playback `hash` / `played_hash` are FNV-1a over the same
little-endian PCM16 bytes and must match after an utterance completes.

Run the production-node host tests from the repository root:

```powershell
.venv/Scripts/python.exe tests/run_rvtts_tests.py
```

The tests compile both real vocoder/PlaybackSource wrappers with CMSIS Stream's
unchanged POSIX platform and deterministic frame engines. They compare every
emitted PCM sample with an independently assembled original whole-utterance
sequence for 1, 2, 19, 92 and 224 feature frames, including repeated utterances,
FARGAN crop/round/flush, fast-producer backpressure and playback before final
generation. A separate two-thread stress check transfers 20,000 blocks through
the production queue and checks every sample. These are scheduling/transport
tests; on-board neural fixtures and vocoder source comparisons remain the
numerical checks.

Measured board results and raw UART are saved separately for each demo:

| Text | LPC first I2S audio | FARGAN first I2S audio | Earlier buffered FARGAN |
|---|---:|---:|---:|
| Hello. | 98 ms | 128 ms | 879 ms |
| One two three. | 118 ms | 149 ms | 1119 ms |
| This is a working demo. | 189 ms | 189 ms | 1528 ms |

Across the streaming checks, 773 FARGAN blocks and 783 LPC blocks completed
with zero hardware underruns, missed callback deadlines, PCM starvation, event
drops, UART errors or pipeline errors. The 213-frame longer sentence, explicit
227-frame rejection, edited CRLF submission and Up-arrow recall also pass.
All producer/playback PCM checksums match. FARGAN `Hello.` also matches the
original full-buffer firmware captured before this change (`9dbfeb0e`).
The final FARGAN restore passes its startup numerical checks and another
93-block `Hello.` delivery, still without errors; it remains running with the
serial port released. Measurements and build/run job identifiers follow below.

- [FARGAN validation](../projects/rvtts_fargan/model/streaming_validation.json)
  and [UART](../projects/rvtts_fargan/model/streaming_uart.txt).
- [LPC validation](../projects/rvtts_lpc/streaming_validation.json)
  and [UART](../projects/rvtts_lpc/streaming_uart.txt).

The old full-buffer throughput measurements remain historical in the original
benchmark/validation files. They do not describe the current playback handoff.

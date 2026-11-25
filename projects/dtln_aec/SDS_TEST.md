# DTLN SDS capture tests

## Why the DTLN capture is complete

DTLN uses a synchronous schedule with one SDS input record and one SDS
output record per iteration. The input record carries 128 microphone samples
and 128 reference samples. The sink writes 128 processed samples with the
same timeslot. At 16 kHz, each iteration covers 8 ms of audio. [Graph definition](app.py) · [Generated schedule](generated/scheduler_app.cpp#L96)

1. `SdsSource` runs first in every iteration. Once it publishes a record, the
   scheduler runs the downstream processing and `SdsSink` before asking the
   source for another record. The sink advances its input FIFO after SDS
   accepts the full output record.
2. After the last input record, the source reports `SDS_EOS` at the start of
   the next iteration. Every previous iteration has already reached the
   sink on the normal completion path. The LCD consumes waveform events on a
   separate thread, and the recording path stays in the dataflow schedule.
3. The demo uses `stream_stop_threads()` to stop and join both runtime threads
   before closing `Dtln_Out`. SDS flushes accepted
   records on close, and SDSIO Server waits for its file writer to finish.
   The demo then closes `Dtln_In`, sets `SDS_STATE_STOP_DONE`, and clears
   `START`. Once both streams are closed and `START` is clear, the server can
   start the next playlist entry with a new capture file.

[SDS source](../../common/nodes_impl/c/SdsSource.hpp) · [SDS sink](../../common/nodes_impl/c/SdsSink.hpp) · [Demo shutdown](demo.cpp#L69) · [Example playlist log](test_data/README.md#example-test-output)

The test input includes 384 leading zero samples and enough trailing zeros
to complete the last hop and flush DTLN's 384-sample algorithmic delay. The
comparison skips the first 768 captured samples, which account for the
leading padding and the graph delay, and compares exactly the original clip
length against the float reference. The capture reader first requires the
exact padded record count, full payloads, and consecutive 8 ms timeslots.
The saved far-end, near-end, and double-talk captures contain 1,365, 1,376,
and 1,340 records respectively. A missing final record or a partial final
payload produces exit code `1`. [Fixture preparation and capture comparison](test_data/sds_demo.py)

## Why another graph can truncate its output

Source EOF proves that all input records have been read. Output completion
depends on the schedule and the nodes' state. A graph that combines several
input hops into one output can reach EOF with a partial block queued. A graph
with filter tails or lookahead may still need extra input to emit its final
samples. An asynchronous branch can still be processing an event when the
source reaches EOF. Each of these cases needs a graph-specific completion
rule, such as padding to a full output block, flushing the processing tail,
or waiting for downstream completion. Close the recording once the sink
has accepted all expected output, then verify the output record count and
timeslots against that graph's rate and alignment.

## Resetting between independent tests

A board reset gives independent test runs a clean starting state. A graph
can keep samples in FIFOs, recurrent state, overlap buffers, pending events,
or peripheral queues. Reinitializing every piece of state requires knowledge
of the whole graph. A board reset is the simplest reliable isolation step
when that cleanup cannot be guaranteed.

The demo supports this through the SDSIO Server `T` or `t` command, which
sets `SDS_FLAG_RESET`. Request the reset after both streams have closed and
`START` is clear. A reset received during playback requests an abort at a node
boundary, so the controller can close a capture before its final hop reaches
the sink. After an explicit reset, let the board reconnect before starting
the next independent test. [Reset handling](demo.cpp#L59) · [SDSIO commands](test_data/README.md#example-test-output)

The automatic `test_demo.ps1` playlist runs the three clips in one boot.
DTLN starts the dataflow and event threads through the CMSIS Stream runtime
for each clip. Before starting them, it calls `reset_fifos_scheduler_app(1)`
to clear the FIFO positions and backing bytes. Identified nodes can reset
their own state through the optional state-reset interface. DTLN uses it
for model recurrence, waveform history, and speaker block assembly. After
the runtime stops both threads, DTLN frees the graph, queue, and runtime
memory before opening the next clip. [Test launcher](test_demo.ps1) · [Scheduler lifecycle](stream_init.cpp) · [Node state interface](../../common/stream/cstream_node.h)

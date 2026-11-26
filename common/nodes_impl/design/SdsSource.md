# SdsSource design

`SdsSource<OUT, outputSamples>` transfers one complete SDS record into its graph
output per firing. The application supplies an open SDS read handle, a pointer
to shared `SdsPlaybackTiming`, the sample rate, and `framesPerRecord` in
`SdsSourceParams`. A frame is one sample per channel, so `framesPerRecord`
must divide `outputSamples`. At initialization the source checks that the
record period is a whole number of SDS milliseconds and a positive whole
number of RTOS ticks within the signed deadline range. It then clears the
shared timing. The payload contains `outputSamples * sizeof(OUT)` bytes.
[Source](../c/SdsSource.hpp) ·
[Shared parameters and timing](../c/SdsEndpoint.hpp)

The application owns stream open and close. The source copies
`SdsSourceParams` at construction. After initialization, the application can
call `bindStream(sdsId_t)` with another open read handle while the scheduler
is stopped or paused. The source updates its copy of the handle, resets
`lastSdsStatus()` to `SDS_OK`, and clears the shared timing so the new pattern
starts with its own first timeslot and release deadline. The record format
and rate come from the initialized node. For the DTLN playlist, the
controller reopens both streams and rebinds the nodes while paused, then
resets graph state before the next pattern fires.
[DTLN pattern transition](../../../projects/dtln_aec/documentation/SDS_TESTS.md#reset-state-and-resume)

The source gives the first read one record period and starts the playback clock
when the first record is published. Each subsequent firing reads one record and
checks that its timeslot advances by one period. If data is ready early,
`osDelayUntil()` blocks the graph thread until the next release tick. The source
sets the next release from the actual release tick so a delayed graph keeps the
hop spacing. The source stores the system timer counts spent in the pacing
delay in `pacingTimerCounts`. The sink reads the latest timeslot and next
release tick from the shared timing on the same graph thread.
[SdsSink timing](SdsSink.md)

The SDS worker prefetches into the SDS circular buffer. The worker can fill it
with data ahead of the graph's release schedule. SDS 3.1.0 exposes no
record-ready notification, so the source sleeps one tick between attempts while
a complete record is missing or the stream lock is busy. The remaining time to
the next release bounds the wait. If no complete record is available by that
tick and SDS reports `SDS_NO_DATA`, the source returns `CG_BUFFER_UNDERFLOW`.
A persistent `SDS_ERROR`, another SDS transfer error, or an invalid record
size or timeslot gives `CG_BUFFER_ERROR`. A failed retry delay gives
`CG_OS_ERROR`, as does a failed pacing delay if the release tick is still
in the future.

The generated synchronous schedule provides room for the output block at
each firing. The source reads into graph-owned storage and publishes the
FIFO block only after a complete transfer, timeslot validation where
applicable, and the pacing wait. If `sdsRead()` returns `SDS_EOS`, `run()` returns
`CG_STOP_SCHEDULER` before publishing another block. `lastSdsStatus()`
retains the most recent raw SDS result for diagnostics.

DTLN uses 128 frames per record at 16 kHz, for an 8 ms period. Each record
contains 256 interleaved floats for microphone and reference. The source reads
from the 16 KiB SDS circular buffer and exposes the hop to the split node. The
tests exercise timed release, SDS read deadlines, tick rollover, record
validation, and when the source publishes to the FIFO. [DTLN graph](../../../projects/dtln_aec/app.py) ·
[Host tests](../../../tests/test_sds_nodes.cpp)

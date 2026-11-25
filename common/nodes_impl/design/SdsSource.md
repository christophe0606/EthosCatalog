# SdsSource design

`SdsSource<OUT, outputSamples>` transfers one complete SDS record into its graph
output per firing. The application opens the SDS read handle and keeps it open
until scheduling ends. The source receives that handle, the sample rate, the
number of samples per channel in each record, and a pointer to shared
`SdsPlaybackTiming` storage. The period must be representable as whole SDS
milliseconds and whole RTOS ticks. [Source](../c/SdsSource.hpp)

The source gives the first read one record period and starts the playback clock
when the first record is published. Each subsequent firing reads one record and
checks that its timeslot advances by one period. If data is ready early,
`osDelayUntil()` blocks the graph thread until the next release tick. The source
sets the next release from the actual release tick so a delayed graph keeps the
hop spacing. The graph can share the latest timeslot and next release tick with
an SDS sink, and connect another node to the source output to inspect the samples.

The SDS worker prefetches into the SDS circular buffer. The worker can fill it
with data ahead of the graph's release schedule. SDS 3.1.0 exposes no
record-ready notification, so the source sleeps one tick between attempts while
a complete record is missing or the stream lock is busy. The remaining time to
the next release bounds the wait. If no complete record is available by that
tick, the source returns `CG_BUFFER_UNDERFLOW`. It reports graph FIFO capacity
failures as `CG_BUFFER_OVERFLOW`, invalid record sizes or timeslots as
`CG_BUFFER_ERROR`, and end of stream as `CG_STOP_SCHEDULER`.

The source reads into graph-owned storage and publishes the FIFO block only
after a complete transfer and the pacing wait. The scheduler thread owns the
FIFO and shared timing. `lastSdsStatus()` retains the raw SDS result for
diagnostics.

DTLN uses 128 frames per record at 16 kHz, for an 8 ms period. Each record
contains 256 interleaved floats for microphone and reference. The source reads
from the 16 KiB SDS circular buffer and exposes the hop to the split node. The
tests exercise timed release, underflow deadlines, tick rollover, record
validation and graph FIFO errors. [DTLN graph](../../../projects/dtln_aec/app.py) ?
[Host tests](../../../tests/test_sds_nodes.cpp)

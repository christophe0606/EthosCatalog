# SdsSink design

`SdsSink<IN, inputSamples>` transfers one graph block to an open SDS write
stream per firing. The application supplies the open handle and a pointer to the
source's `SdsPlaybackTiming` in `SdsSinkParams`. The sink copies those
parameters at construction and requires both fields during initialization.
Each record contains `inputSamples * sizeof(IN)` bytes. The sink uses the
most recently published source timeslot for its record and the next source
release tick for its retry deadline. In the host reblocking test, each
output combines two source blocks and uses the second block's timeslot.
[Sink](../c/SdsSink.hpp) ·
[Source timing](SdsSource.md)

When a full input block is missing, `prepareForRunning()` returns
`CG_SKIP_EXECUTION` and `run()` returns `CG_BUFFER_UNDERFLOW`. The source
must publish timing before the first write. With a full input block and
`timing.started` still false, `run()` returns `CG_BUFFER_ERROR`. The sink
keeps the block queued until `sdsWrite()` accepts the full payload.
For `SDS_NO_SPACE` or `SDS_ERROR` from a busy stream lock, it sleeps one tick
and retries within the current hop. If SDS still reports `SDS_NO_SPACE` at
the deadline, the sink returns `CG_BUFFER_OVERFLOW`. A short write, another
SDS transfer error, or a persistent `SDS_ERROR` gives `CG_BUFFER_ERROR`.
A failed retry delay gives `CG_OS_ERROR`. `lastSdsStatus()` retains the
most recent raw SDS result.

After SDS accepts the full payload, the sink consumes the block from the graph
FIFO. The SDS worker can send the buffered record to the host later. The
application closes the write handle after the graph finishes producing the
expected output so SDS can flush the capture.

To reuse the sink, call `bindStream(sdsId_t)` with the open handle for the
next capture while the scheduler is stopped or paused. This replaces the
handle and resets `lastSdsStatus()` to `SDS_OK`. The source's
`bindStream()` clears the shared timing, and the next source record
initializes it for the new pattern.
[DTLN pattern transition](../../../projects/dtln_aec/SDS_TESTS.md#reset-state-and-resume)

In DTLN the input is one mono hop of 128 floats. The graph also connects the
processed hop to dedicated LCD and speaker nodes. Host tests check the timeslot
and payload, write retry timing, and FIFO preservation on failure.
[DTLN graph](../../../projects/dtln_aec/app.py) ·
[Host tests](../../../tests/test_sds_nodes.cpp)

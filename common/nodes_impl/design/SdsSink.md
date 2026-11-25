# SdsSink design

`SdsSink<IN, inputSamples>` transfers one graph block to an open SDS write
stream. The application supplies the SDS handle and a pointer to the graph's
`SdsPlaybackTiming`. The sink uses the most recently published source timeslot
for its record and takes its write deadline from the next source release tick.
With reblocking, the output record uses the timeslot of the last source block
contributing to the firing. [Sink](../c/SdsSink.hpp) ?
[Source timing](SdsSource.md)

The sink checks the graph input FIFO for a full block and returns
`CG_BUFFER_UNDERFLOW` when the block is missing. The block stays queued while
`sdsWrite()` cannot accept it. If the SDS buffer is full or its lock is busy,
the sink sleeps one tick and retries while there is time left in the current
hop. If the buffer is still full at the deadline, the sink returns
`CG_BUFFER_OVERFLOW`. A short write, other SDS error or persistent lock failure
returns `CG_BUFFER_ERROR`. `lastSdsStatus()` retains the raw SDS result.

After SDS accepts the full payload, the sink consumes the block from the graph
FIFO. The SDS worker can send the buffered record to the host later. The
application closes the SDS write handle after the graph thread finishes so SDS
can flush the capture.

In DTLN the input is one mono hop of 128 floats. The graph also connects the
processed hop to dedicated LCD and speaker nodes. Host tests check the timeslot
and payload, write retry timing, and FIFO preservation on failure.
[DTLN graph](../../../projects/dtln_aec/app.py) ?
[Host tests](../../../tests/test_sds_nodes.cpp)

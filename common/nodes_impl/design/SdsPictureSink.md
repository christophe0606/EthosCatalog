# SdsPictureSink design

`SdsPictureSink(name, params, identified=False)` accepts
`picture(uint32_t timeslot, BufferPtr pixels)` on `iev0` and writes one SDS
record per event. Its Python `params` argument is a C++ expression for the
application's `SdsPictureSinkParams`. Declare that parameter object in
`app_config.hpp` after including `SdsPictureEndpoint.hpp`.
[Python description](../../nodes/SdsPictureSink.py) ·
[C++ implementation](../c/SdsPictureSink.hpp) ·
[Parameters](../c/SdsPictureEndpoint.hpp)

For replay and recording in the same graph, connect the picture output to
the sink and route the source's status output to the playback controller.

```python
graph.connect(source["oev0"], sink["iev0"])
```

The application opens the write stream before graph initialization and sizes
its SDS stream buffer for a complete picture including the SDS header. Set
`pictureBytes` to the exact payload size. The producing node and the SDS
recording configuration must agree on the pixel format, dimensions, and
stride. Close the write stream after recording so SDS can flush the capture.

## Payload and ownership

The sink checks the event selector, payload types, and buffer size. It holds
a shared lock while passing the existing pixel address directly to
`sdsWrite()`. Each output record preserves its event's timeslot. The sink
does not keep a reference after the write attempt finishes. If the event
came from `SdsPictureSource`, the last reference returns the block to the
source's configured pool. Other producers can send the same event type with
their own CMSIS Stream `BufferPtr` storage.

## Write backpressure

The sink allows `timeoutTicks` for a write. Zero makes one SDS attempt.
It retries `SDS_NO_SPACE` and `SDS_ERROR` from a potentially busy stream lock
at one-tick intervals until the deadline. The buffer stays locked and
available for the full transfer. An exhausted retry window with
`SDS_NO_SPACE` returns `CG_BUFFER_OVERFLOW`. A malformed event, null or
wrong-size buffer, short write, persistent `SDS_ERROR`, or other SDS transfer
failure returns `CG_BUFFER_ERROR`. A failed buffer lock or retry delay
returns `CG_OS_ERROR`.

The sink returns failures to the event runtime. The playback source's
overflow policy applies to source operations. `lastSdsStatus()` retains the
most recent raw SDS write result. Host tests check picture address reuse
between read and write, timeslot and payload preservation, bounded write
retries, and release of the pool block after the last reference.
[Playback design](SdsPictureSource.md) ·
[Host tests](../../../tests/test_sds_nodes.cpp)

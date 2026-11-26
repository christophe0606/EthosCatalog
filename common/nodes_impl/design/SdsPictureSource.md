# SdsPictureSource design

`SdsPictureSource(name, params, identified=False)` reads one fixed-size SDS
picture per request. The Python `params` argument is a C++ expression for the
application's `SdsPictureSourceParams`. Declare the parameter object in
`app_config.hpp` after including `SdsPictureEndpoint.hpp`.
[Python description](../../nodes/SdsPictureSource.py) ·
[C++ implementation](../c/SdsPictureSource.hpp) ·
[Parameters](../c/SdsPictureEndpoint.hpp)

| Port | Event | Delivery |
| --- | --- | --- |
| `iev0` | `kDo` | Request one SDS record |
| `oev0` | `picture(uint32_t timeslot, BufferPtr pixels)` | Queued after a complete read and timestamp validation |
| `oev1` | `pictureEnd` | Queued without a payload when SDS reports end of stream |
| `oev1` | `pictureError(int32_t status)` or `pictureDropped(int32_t status)` | Delivered directly to graph subscribers and the configured application handler |

## Playback clock

The application sets the playback rate through the timing of `kDo` requests.
When the frame period is a whole number of RTOS ticks, use a CMSIS-RTOS2
periodic timer. At a 1 kHz tick rate, 25 fps takes 40 ticks per frame. Start the
timer after the graph is ready. Its callback enqueues the request, and the
graph event thread performs the SDS I/O. Use `identified=True` when retrieving
the source by its generated node ID.

```cpp
const bool queued = queue.push(LocalDestination{source, 0},
                               Event(kDo, kNormalPriority));
```

Here `queue` is the running graph's event queue and `source` is the C++
node pointer. The application handles a rejected `push` because the source
never receives that request. To avoid bursts, track at most one outstanding
request and skip timer ticks until its picture or status arrives. Set the
pending flag before calling `push`, clear it if `push` fails, and clear it
when the graph receives the corresponding picture or status. Share the
flag safely between the timer callback and the graph event thread.

For fractional periods, an existing application thread can wait with
`osDelayUntil()` using absolute deadlines. Compute each deadline by adding
`floor(frameIndex * tickFrequency / playbackFps)` to the start tick, with a
64-bit intermediate for the multiplication. A one-shot timer can also use
the differences between successive deadlines. At 30 fps and a 1 kHz tick
rate this gives 33- and 34-tick intervals. When the controller misses a
deadline, advance to the next future deadline before posting again.

Stop posting on `pictureEnd` or `pictureError` and stop the timer before
destroying the graph. A `pictureDropped` notification completes the current
request and allows the next timer tick to proceed.

## Record validation

The application opens an SDS read stream and sizes its SDS stream buffer for
a complete record including the SDS header. Set `pictureBytes` to the exact
payload size. The pixel format, dimensions, and stride are defined by the
application's SDS configuration. The first complete record establishes the
timestamp baseline.

Set `framesPerSecond` to the expected rate of the recording when SDS timeslots
are in milliseconds. This field checks the recording and the request clock
sets the actual playback rate. The default value of zero checks timestamp
order only. With a rate from 1 to 1000 fps, the source accepts steps from
`floor(1000 / framesPerSecond)` through `ceil(1000 / framesPerSecond)`
milliseconds. At 30 fps this accepts 33 or 34 ms per step.

The source uses unsigned subtraction for the 32-bit timeslot delta. A forward
step across wraparound is valid when its delta is in `[1, INT32_MAX]` and
meets the configured interval check. A duplicate, backward step under this
half-range rule, or interval below the minimum returns `CG_BUFFER_ERROR`
under either overflow policy. An otherwise valid forward step above the
maximum is a timestamp gap and uses the overflow policy.

## Pool ownership

The application supplies a CMSIS-RTOS2 memory pool. Each block must hold at
least `SdsPictureSourceParams::poolBlockBytes(pictureBytes)` bytes. The
leading bytes store the pool handle and the rest contain the image. The
source passes the pixel address directly to `sdsRead()`. It wraps a valid
record in a CMSIS Stream `BufferPtr` whose underlying `UniquePtr` has a
custom deleter. Event delivery shares ownership of the same pixels.

When the last reference is released, the deleter uses the stored handle to
return the original block to its pool. This works after the source is
destroyed. Size the pool for all pictures that may be queued or retained
downstream at once. Keep it alive through graph teardown and until all
queued events and retained buffers have been released.

## Overflow and errors

Set `overflowPolicy` to `SdsPictureOverflowPolicy::Fail` (the default) or
`SdsPictureOverflowPolicy::Skip`.

| Condition | Status | Fail | Skip |
| --- | --- | --- | --- |
| Output pool exhausted | `CG_MEMORY_ALLOCATION_FAILURE` | Return the error before reading | Read and discard one complete record using the reserved block |
| Picture delivery rejected by the event queue | `CG_EVENT_QUEUE_FULL` | Return the error after reading | Release the source's reference to the consumed picture |
| Timestamp gap above the configured maximum | `CG_BUFFER_OVERFLOW` | Return the error after reading | Discard the record and use its timeslot as the next baseline |

Skip reserves one extra full-size pool block at initialization for discard
reads. A successful skip emits `pictureDropped(status)` and returns
`CG_SUCCESS` so playback can continue. Picture fan-out uses one queue entry
per subscriber. Entries already accepted can be delivered when a later
subscriber's enqueue is rejected.

The source retries `SDS_NO_DATA` and `SDS_ERROR` (which SDS can use for a busy
stream lock) at one-tick intervals up to `timeoutTicks`. Zero makes one
attempt. An expired read with `SDS_NO_DATA` returns `CG_BUFFER_UNDERFLOW`.
A short record, persistent `SDS_ERROR`, or other SDS transfer failure
returns `CG_BUFFER_ERROR`. A failed retry delay returns `CG_OS_ERROR`.
If the queue rejects the end-of-stream event, the source reports
`CG_EVENT_QUEUE_FULL` as a fatal error. `lastSdsStatus()` gives the latest
raw SDS result for diagnostics.

For runtime failures, the source delivers `pictureError(status)` before
returning the error. Both `pictureError` and `pictureDropped` run status
callbacks synchronously on the event thread, including when the queue is
full. Register the application callback with `EventQueue::setHandler()`.
The callback receives the source's node ID when identified.
Keep callbacks short and have them signal the controller for teardown. The
CMSIS Stream runtime can also report its standard `kError` after a fatal
return. Initialization failures are returned by graph initialization.

Host tests cover wraparound, fractional millisecond intervals, pool and
queue overflow, recovery after a timestamp gap, and notification delivery
to both the graph and the application.
[Recording design](SdsPictureSink.md) ·
[Host tests](../../../tests/test_sds_nodes.cpp)

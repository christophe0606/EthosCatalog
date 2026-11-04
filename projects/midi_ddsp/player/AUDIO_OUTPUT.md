# Audio output

`audio_output.cpp` adapts the project to the DevKit E8 SAI3/WM8904 output in
`board/DevKit-E8/audio_output.cpp`. It accepts 256 mono PCM16 samples at 16 kHz,
duplicates them into left/right 32-bit I2S slots and copies them into a six-slot
queue. The producer waits for a completed slot before reusing it. Audio starts
after two blocks (32 ms); queued audio, including the active block, is bounded
at 96 ms. The codec/hardware FIFO adds a small amount beyond this queue bound.
The Reverb256 grouping separately adds at most 12 ms of accumulation delay.

The current board RTE selects nonblocking, interrupt-fed I2S3, with DMA disabled.
The CPU interrupt handler reads the DTCM buffers. Compile-time guards reject
changing this to DMA or blocking operation without adapting the implementation.
No changes to the imported SAI or codec drivers are needed. Initialization uses
the existing speech project's 16 kHz setup: board Y4 at 12.288 MHz, codec
SYSCLK/fs 768, I2S 32-bit words and stereo slots. Codec configuration is read back
before use. References: WM8904 Rev4.1 pp.94,101 Tables54,62 and board schematic
220-00319-B p.7.

Headphone volume is set to driver value78, which maps to codec volume code49
(-8 dB). The original value60 mapped to code37 (-20 dB); the first requested
increase used value69/code43 (-14 dB). The user then requested another6dB:
the current setting is12dB above the original, approximately four times its
output amplitude. Both channel volume codes are read back at startup. The PCM
signal itself is unchanged by this analog gain setting.

`devkit_e8_audio_stats` is debugger-visible. Queue/completion counts and maximum
occupancy describe ownership. `starvation_events` counts distinct empty-queue
gaps during active production, while `silence_blocks` counts all substituted
zero blocks, including intentional end-of-stream silence. `drain()` suppresses
the normal final gap and waits for the hardware FIFO before return. A subsequent
write resumes active production accounting. `driver_underflows` records events
reported by the driver; the installed interrupt driver does not report every
physical FIFO underflow. Callback spacing exceeding 18 ms is counted separately
as a deadline miss. A block should take 16 ms. Interpret these measurements
together in a live playback run.

`first_audio_ticks` is the timestamp at which the first application block is
submitted to SAI. Completion timestamps mean data has reached the driver or
hardware FIFO, rather than an independently measured analog output. Timer ticks
wrap modulo 2^32. Inspect the statistics while the CPU is halted.

The deterministic host test compiles the production queue against small mocks
and checks 40-block ordering/wrap, stereo conversion, buffer lifetime, capacity,
startup, drain, restart, starvation/resume and a stalled consumer's timeout:

```
python tests/run_midi_ddsp_audio_queue_tests.py --cxx clang++
```

Use `--vcvars <vcvars64.bat>` with MSVC. Host checks passed. The final
[b-91/d-92 board run](../model/stream_final_b91.json) completed the included
104-second score and tail with 6,688 queued/completed blocks, zero active
starvation or audio deadline misses, and left/right codec readbacks of49.

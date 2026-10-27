# Hybrid FARGAN checkpoint

Updated 2026-10-27. The user authorized CPU FARGAN and CMSIS-DSP/Helium
optimization after accepting the LPC speaker sound.

Completed:

- Four shared Ethos methods retained: prosody pre, paired GRU step, prosody head,
  complete acoustic tile. Project-owned shared caches match the LPC feature path.
- Original CPU FARGAN neural/synthesis order and LUT activations reconstructed
  from preserved BSD functions; upstream files remain unchanged.
- Source float matrices requantized per output row to int16, with dynamic int16
  inputs and int64 accumulation. Current packed cache payload 1,661,600 bytes,
  CRC 54ca3f64, stored in OSPI at 0xC1000000. Startup copies 1,056,416 bytes to
  SRAM0 and 605,184 bytes to DTCM (three hot GRU input matrices).
- Host complete recurrent 173-frame / 27,680-sample comparison PASS: source PCM
  SNR 51.800 dB, RMSE 0.0000648218, max difference 0.000592803. Gates declared
  before board: SNR >=30 dB, RMSE <=0.001, max <=0.01. CPU scalar/MVE host
  comparison board startup additionally requires max error <=0.001.
- CMSIS-DSP absolute maximum on full vector lengths plus MVE long int16 dots;
  no inline asm. Original 10,000-float skip scratch bounded to actual 688 floats in
  generated project-owned adaptation.
- Full UART echo/editing frontend, I2S3/WM8904 audio and landscape animated LCD
  graph copied/adapted from the accepted LPC architecture. Separate audio_frames
  handles the original 80-sample trim/flush and N+1 audio blocks from N feature frames.
- Graph generates from both root and project folder: 7 schedule entries / 368 B FIFO.
- Own AI layer regenerated successfully: 528,144-byte PTE, CRC 7e22fd20,
  four automatic Ethos delegates, project-local dictionary/vocabulary assets
  and CPU weight metadata, including current CRC 54ca3f64. No heavy host process remains running.
- Cproject uses 192 KiB method + 128 KiB temp pools, based on measured shared
  network peaks 166072/93216 bytes. Board build fits SRAM0: 4,036,708 / 4,194,304
  bytes. The coordinating agent verified this through build-artifact tools.
- Hybrid builds b44/b47 and board run r48 pass the source accuracy test:
  173 frames, MVE=1, source max 0.000597819, RMSE 0.0000636759, SNR 51.9549 dB,
  optimized-host max 0.0000875145. Full UART/I2S/LCD demo reports no audio errors.
- Four-row r48 Hello baseline is too slow: 92 feature frames / 93 audio frames,
  vocoder 2152.462 ms, init 24.804 ms, individual frame 22.624–22.647 ms;
  full synthesis 2676.435 ms / 930 ms audio, RTF 2.878. Historical exact source
  and metrics are preserved in model/cpu_benchmarks/r48_*.
- Two-row build b49 / run r50 passes identical board accuracy but improves
  Hello vocoder time only 4.08%, to 2064.543 ms, with 21.699–21.726 ms CPU frames
  and 23.836 ms priming. Its exact source/measurements are in model/cpu_benchmarks.
- Current candidate packs four rows per eight input columns into one sequential
  weight stream. Scalar and MVE consume the same packed int16 values with exact
  int64 sums. The three GRU input matrices occupy 605,184 bytes ordinary DTCM,
  freeing the same SRAM0 capacity for a parent-owned shared-PTE cache experiment.
  Startup identifies rows=4 packed=1 dtcm=605184. Standalone M55 hot loop has
  five vector loads, four long dots, one LE, no inner-loop scalar stack access.
  Full 173-frame packed-host validation passes with identical source metrics:
  SNR 51.800011 dB, max error 0.000592802651, RMSE 0.0000648218378. Host build
  exercises the same packed layout and split-cache binding as the MVE build.
- Earlier NPU-only failures and all benchmark evidence remain historical;
  source/config snapshots are in npu_benchmark/.

- Final packed/DTCM/SRAM-PTE Release b-53, confirmed current by b-56, runs as
  r-57. All four Ethos fixtures and the 173-frame CPU source check pass:
  source SNR 51.9549 dB, maximum 0.000597819, RMSE 0.0000636759 and maximum
  optimized-host delta 0.0000875145.
- Full synthesis takes 771.137 / 1014.411 / 1421.190 ms for 930 / 1230 /
  1730 ms audio, RTF 0.82918 / 0.82472 / 0.82150. Complete CPU frames take
  7.354-7.390 ms per 10 ms audio frame. Hello CPU time is 3.07x faster than r48.
- All 389 audio blocks / 62,240 samples complete with zero underruns, deadline
  misses, event drops and UART/pipeline errors. LCD presents 2266 frames with
  56 coalesced updates during inference. UART echo, Backspace/Delete, six-second
  status suppression while editing, single CRLF submission and 227-frame
  rejection pass. The edited fourth Hello brings delivery to 482 blocks,
  77,120 samples, still with zero errors.
- Actual final SRAM0 usage is 3,959,684 / 4,194,304 bytes; ordinary DTCM is
  764,164 / 884,736 bytes, including the hot cache at 0x2000de80; MRAM is
  878,064 bytes. Heap and stack reservations are separate. PTE copy and CRC
  checks use SRAM0 and NPU SRAM AXI ports; the existing board callbacks perform
  cache maintenance and barriers. Imported SystemInit enables CPU caches.
- Independent final review finds no source correction required. Evidence is in
  model/cpu_board_validation.json and model/cpu_board_uart.txt.

Acceptance:

- The user completed the FARGAN speaker check on 2026-10-27 and reported:
  "FARGAN sounds much better". Both variants have now passed listening.
- FARGAN is included in the catalog. The current `model/benchmark.yml` records
  the accepted hybrid demo; the previous NPU-only result is preserved as
  `model/benchmark_npu_81.yml`.
- Release r-57 remains running, with COM5 released. No required work remains
  for the requested port, real-time checks, UART echo, speaker and LCD demo.

Parent coordinates board work and shared checkpoint .flow/rvtts/CHECKPOINT.md.
No board or firmware build has been performed by the CPU implementation agent.

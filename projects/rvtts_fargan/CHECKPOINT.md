# FARGAN checkpoint

Updated 2026-10-27. Status: **experimental cproject; board execution established,
real-time and strict vocoder numerical validation failed**.

Completed:

- Project-owned six-method ExecuTorch/Vela layer: 2,608,624-byte PTE, six
  Ethos-U85 delegates. Model CRC and both source-asset CRC checks pass on board.
- Separate Debug, Release and ProgramModel target sets preserve LPC wiring.
- Release build `b-27`: Arm Compiler 6.24.0, `-O3 -ffast-math`.
- CMSIS ProgramModel `b-25`/`d-26`: stage 5, ready 1, errors zero. CMSIS flash
  programmed 3,223,552 aligned bytes. Final uninterrupted run `r-28` completes
  all bounded tests and reports numerical FAIL with two failures.
- Shared method fixtures pass. FARGAN init maximum error 0.007872820 exceeds
  0.000184089; frame maximum error 0.037712593 exceeds 0.002012338. No tolerance
  weakening or model change was made to hide these errors.
- Complete 10 ms vocoder frame median 108.257958 ms. Full 81-frame synthesis
  median 9,562.321145 ms for 820 ms PCM, RTF 11.661367. This excludes frontend,
  UART, LCD and I2S, so the measured configuration cannot sustain real-time audio.
- SRAM0 2,078,752/4,194,304 bytes. Method/temp pool peaks 184,852/93,216 bytes;
  memory is not the observed blocker.
- Host fixture/cache checks reproduce the quantized fixture outputs exactly.
  Init outputs are bit-identical with all pitch indices forced to 0 or 255;
  its board mismatch is therefore not explained by selecting different valid
  pitch indices. Board discrepancies are 172/75 output LSB for init/frame,
  beyond final quantization rounding. The internal lowering discrepancy remains
  unresolved, and frame index handling is not independently cleared.
- Board PCM synthetic SNR for 2/19/81 input frames is 13.445/13.779/5.289 dB
  versus the float complete-schedule reference, with zero clipped samples.
  These tests differ from the 173-frame host quantized result of 13.109 dB.
  No speaker listening validation has occurred for FARGAN.

Evidence is in [model/BENCHMARK_REPORT.md](model/BENCHMARK_REPORT.md),
[model/benchmark.yml](model/benchmark.yml), `model/benchmark_*.yml`,
`model/benchmark_uart.txt`, `model/benchmark_memory.txt`,
`model/benchmark_details.json` and `model/benchmark_diagnosis.json`.
All 12 FARGAN benchmark YAML files validate against the repository schema.

First-phase disposition: retain the complete converted model and runnable
benchmark as a checkpoint. Do not claim a usable real-time FARGAN demo or begin
broad speed/quality optimization. Restore the LPC OSPI model and Release demo
for the user's required speaker listening acceptance gate. Future FARGAN work
would first need to resolve numerical lowering differences and then demonstrate
an order-of-magnitude frame-time improvement with new measurements.

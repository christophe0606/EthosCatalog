# RNNoise INT8 hybrid baseline benchmark

Release build b-11 and load/debug d-12 on Alif DevKit-E8 ran the original-quantization
168,040-byte PTE with seven automatic Ethos-U85 delegates and CPU dense convolutions.
AC6 6.24.0 used `-O3 -ffast-math`. CPU, AXI/NPU and system timer were all measured
at 400 MHz using SE services and the RTOS timer API.

After two warmups, ten checked fixture forwards took **35.0492 ms median**,
35.0387?35.1255 ms. Summed NPU PMU time was only **0.008876 ms median**
(3459?3597 cycles). The complete forward includes CPU arithmetic, dequantization,
convolutions, all delegates and PMU hooks; tensor setup, method-load check, output
copying, validation, UART, feature extraction, synthesis and LCD are excluded.
This baseline exceeds the 10 ms / 480-sample deadline by 3.5 times. It cannot
support continuous full-rate RNNoise suppression and must not be presented as
real-time. Dense-layer delegation should be corrected before final scheduling.

The cold-state upstream fixture and all five outputs of a separate 160-frame
independently recurrent synthetic sequence matched imported PyTorch exactly
(0 LSB maximum difference). The source TFLite sequence still differs by gain
MAE 0.0239590704 and VAD MAE 0.00576171977, agreeing with the host conversion
findings. These are synthetic conversion checks, not speech-quality evaluation.
`pipeline_error`, method-load error and forward error were zero; runtime test,
fixture parity and benchmark completion flags were one. Every measured forward
required seven nonzero PMU invocations, equal cycle/active sums and no overflow.
The final raw accumulator includes the last timed forward plus 160 replay frames:
1127 invocations, cycles=active=573926, overflow=0.

Method and temporary allocator peaks were 392,968 and 864 bytes. The project
reserves 512 KiB method and 256 KiB temporary pools in SRAM0. The original 128 KiB
method pool failed with ExecuTorch error 0x21 (MemoryAllocationFailed), because
the graph alone plans a 353,264-byte buffer. Increasing the project-owned pool
resolved this; no pack/library was modified. Build memory totals are MRAM
643,568 bytes, SRAM0 786,448 bytes and DTCM RW 134,372 bytes plus the separately
reserved heaps/stacks, leaving room for LCD double buffering.

`benchmark.yml` passes the repository schema and records model/firmware hashes,
all samples and measurement scope. `benchmark_debug.txt` is the debugger capture;
`benchmark_build_memory.txt` is the CMSIS build-artefact memory report. No UART
capture is claimed (available COM3/COM7 yielded no text). A user-authorized clean
`cbuild --rebuild` and a fresh session resolved an initially stale YAMNet debugger
symbol path. Final `get_device_info` identified the correct RNNoise ELF and target.
Firmware was resumed and left running after the final inspection.

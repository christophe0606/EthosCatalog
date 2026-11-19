# DTLN-aec hardware benchmark status

The initial direct-OSPI Release run measured both methods on DevKit-E8.
The CMSIS Solution extension had returned `CMSIS_NO_SOLUTION` for the earlier ProgramModel rebuild
because `cmsis-csolution.getSolutionFile` was unavailable. Explicit solution
activation returned `TASK_FAILED` because `cmsis-csolution.activateSolution`
was unavailable. No firmware image was built or loaded by those attempts.

A dedicated VS Code profile with Python Environments disabled restored
solution-command access. Its environment preflight then found that the
generated CMSIS tools environment points to a Toolbox installation removed
when the Solution extension version changed. After VS Code regenerated
the tools environment, the clean ProgramModel rebuild `rb-1` succeeded.
ProgramModel reached its verified preparation state. The first model
flash timed out during programming at 52 seconds. Reloading ProgramModel
and retrying with a 180-second cap completed successfully.

## Initial direct-OSPI measurements

Release rebuild `rb-4` and load `d-5` ran the 1,763,792-byte
PTE at `0xC3C00000` with CRC `0x5AE153B6`. The debugger initially
tried the previous ProgramModel AXF and failed to attach. The Release
firmware ran and emitted the complete measurements over UART.

| Method | Median NPU | Median full adapter | Max full adapter | Max output error | Max state error |
| --- | ---: | ---: | ---: | ---: | ---: |
| stage1 | 4.521721 ms | 23.519826 ms | 23.522295 ms | 0.023907483 | 0.057539448 |
| stage2 | 11.407938 ms | 39.240010 ms | 39.242003 ms | 0.000531930 | 0.128538132 |

Both methods returned finite outputs, with one and two Ethos invocations
respectively and valid PMU samples. Both failed the pre-Vela fixture limits.
The startup check stopped the demo before streaming. The full-adapter
times already exceed the shared 8 ms hop budget. The link placed 792,592
bytes in SRAM0 including pools, with a 595,504-byte MRAM image.
[Initial UART capture](benchmark_uart_ospi_initial.txt)

## Initial SRAM-staged measurements

ReleaseSRAM rebuild `rb-6` and load `d-7` used the same PTE from
the verified SRAM0 copy at `0x02000000`. Linked SRAM0 usage was
2,556,400 of 4,194,304 bytes. The MRAM image was 595,760 bytes.

| Method | Median NPU | Median full adapter | Max full adapter |
| --- | ---: | ---: | ---: |
| stage1 | 0.202681 ms | 4.852924 ms | 4.855998 ms |
| stage2 | 0.634738 ms | 6.835028 ms | 6.840080 ms |

The original pre-Vela fixture errors exactly matched the direct-OSPI
run. A debugger read of all 1,793 output floats matched the independent
integer TOSA reference across both methods. This isolated the discrepancy
to the comparison reference. The initial SRAM method sum was 11.69 ms,
above the 8 ms hop interval before DSP. Follow-up ReleaseSRAM testing
uses the existing guarded CPU operator overrides from MIDI-DDSP.
[Initial SRAM UART capture](benchmark_uart_sram_initial.txt)

## Verified ReleaseSRAM measurements

Build `b-11` and load `d-12` include the guarded CPU kernels and the
integer TOSA startup fixtures. Both methods returned zero output and state
error with valid PMU samples. The 1,763,792-byte PTE and its Vela partitioning
are identical to the initial runs.

| Method | Median NPU | Median full adapter | Max full adapter |
| --- | ---: | ---: | ---: |
| stage1 | 0.202724 ms | 0.666964 ms | 0.671068 ms |
| stage2 | 0.634731 ms | 1.071604 ms | 1.073473 ms |

The method allocator peaks after stage 1 and stage 2 were 13,356 and
30,884 bytes. The temporary pool peaks at those points were 5,168 and
255,168 bytes. The linked image uses 606,224 MRAM bytes and 2,556,400
SRAM0 bytes including the cached PTE and reserved pools.

The complete fake pipeline's measured maximum was 2.0631575 ms across
9,640 completed hops. The separate streaming record includes both methods,
DSP, synthetic sample generation and sink checks. It excludes the source
sleep and serial reporting. [Final UART capture](benchmark_uart_sram_release.txt)

## Harness

`../benchmark.cpp` invokes `stage1` and `stage2` independently from the same PTE.
Each method uses its own warm-state fixture in `model_test_data.h`, restores
all three inputs on every call, performs two warmups and records ten samples.
The samples retain summed Ethos PMU cycles, complete adapter timer ticks, and
module execution timer ticks. The adapter includes SRAM input copies, tensor
setup, execution and output/state copies. Module timing includes the ExecuTorch
call and its host/operator work. Display, test-pattern generation, FFT,
overlap-add and serial reporting are outside those intervals.

The startup checks require one completed Ethos invocation for `stage1` and two
for `stage2`, nonzero PMU cycles, matching active cycles and zero overflow.
The predeclared maximum absolute errors are 0.005 for the stage1 mask,
0.0005 for the stage2 block and 0.05 for either recurrent state. Current
expected outputs come from the independent integer TOSA model in
`integer_test_data.h`. The initial captures retain the results of the
pre-Vela fixture comparison. Float bit checks catch non-finite output
with fast-math enabled.
The harness reports every numeric result before returning a failed check.

The project reserves 256 KiB for method data and 512 KiB for temporary data in
SRAM0. The runtime copies inputs to NPU-visible SRAM0 and keeps the 1,763,792-byte
PTE in place at `0xC3C00000`. Before loading it, runtime initialization uses the
guarded 100 MHz OSPI read-clock API and verifies the entire PTE CRC32. These
reservations require build and runtime high-water verification on the board.

The `DevKit-E8@DTLN_AEC_ReleaseSRAM` target selects the runtime cache with
`DTLN_MODEL_IN_SRAM: 1` and
`NPU_MEM_ATTR_2: 0x20`, `NPU_MEM_ATTR_3: 0x20`. Keep these three definitions
together in the selected build configuration. The compile-time check rejects
an SRAM cache using the direct-OSPI NPU attributes. This path adds a PTE-sized
SRAM0 reservation, verifies the original OSPI CRC, copies once, checks the
cached CRC and passes the cached pointer to ExecuTorch. Shared board callbacks
clean writable SRAM caches before NPU access. The default uses direct OSPI.

`dtln_benchmarks[0]` and `[1]` expose raw samples, maximum fixture errors and
allocator high-water marks to the debugger. Pool peaks are cumulative since
boot. Stage2's reported method peak includes the already loaded stage1 method,
and temporary peak is the maximum reached by either method up to that point.
Last-delegate region diagnostics describe only the final delegate of a method.
They do not represent the combined regions of both stage2 delegates.

## Run and record

1. Activate `EthosCatalog.csolution.yml` in the CMSIS view after restoring the
   CMSIS Solution extension. Select `DevKit-E8@DTLN_AEC_ProgramModel`, verify no
   stale DTLN build output, and rebuild through CMSIS.
2. Prepare and program the project's model-only run file using the repository
   OSPI programming procedure and CMSIS tools.
3. Select `DevKit-E8@DTLN_AEC_ReleaseSRAM` and rebuild with the solution's
   `-O3 -ffast-math` settings. Use the serial tools to capture startup output
   from the board console at 115200 baud. Require `DTLN_Test_result PASS`
   and both method records with ten valid samples. The
   `DevKit-E8@DTLN_AEC_Release` target is the direct-OSPI comparison.
   Both targets use the same programmed PTE.
4. Record separate `benchmarks.networks` entries named `DTLN-aec stage1` and
   `DTLN-aec stage2`, each with `pte: ai_layer/model.pte` and its exact exported
   method. Use the observed clock frequencies to convert samples. Preserve
   the serial capture and build/load job identities. Label the allocator scope
   as cumulative in the provenance. Validate with
   `scripts/validate_benchmarks.py` after creating the measured record.

Vela estimates are preserved in `vela_report.txt`. The observed method
measurements and results are recorded in [benchmark.yml](benchmark.yml).

[OSPI programming procedure](../../../documentation/alif-e8-flashing.md#program-an-ospi-model)

# Handwriting transfer optimization — completed

## Current status after resuming on 2026-10-22

Later user-requested follow-up: UART typing/editing echo is implemented and
loaded in Release build b-6 / load l-7. COM7 verified partial-line echo before
Enter and backspace plus CRLF submission. Host suites passed; the board is
running and serial released. See the UART echo entry in LIVE_VALIDATION.md.
The transfer optimization results below still apply.

The work below was resumed and completed. The normal Release firmware is built
and loaded (`b-4`, `l-5` in the resumed CMSIS session), with the benchmark define
disabled. The board is running; COM7 has been released. Startup fixtures and
model CRC passed. The final demo accepted `Hi`, emitted 49 points and completed
on one line in 1,753 RTOS ticks at 1 kHz. No new visual acceptance is claimed.

DMA1 was slower at all measured sizes, so the production state copies remain
on the CPU in DTCM. Instead, the engine now reads borrowed method outputs within
each stage and reuses unchanged input slices. This removes 20,412 copied bytes
per stroke and saves 6,400 bytes per Engine (20,288 to 13,888 bytes).

The same-binary copied/direct-output comparison passed all six 64-step runs
with the baseline hash `65bfb6d3`. Direct output saved about 16.26 us per step
(0.048%); this is a small isolated latency improvement. Host C++ suites and
both graph tests passed, including retries at every failed stage. The benchmark
is retained as an opt-in source, compiled out normally. Full results, ownership
contracts and reproduction are in [TRANSFER_OPTIMIZATION.md](TRANSFER_OPTIMIZATION.md).

Final production memory report: MRAM LOAD usage 517,488 bytes, SRAM0 LOAD usage
1,732,624 bytes; RW_RAM execution region 158,564 bytes. Reserved heap/stack
sizes and model/pool configuration are unchanged. The source changes are
uncommitted. No required task remains; future performance work should target
the dominant model/OSPI execution rather than introducing small DMA copies.

The remainder of this file is the **historical paused handoff**. Its statements
about pending work, enabled benchmarks and old board/debug state are superseded
by this completion record.

## Original paused handoff

Saved on 2026-10-22 at the user's request before leaving for a meeting.
At that pause the optimization was incomplete; see the completion record above.

## Request and latest direction

The user asked to optimize `projects/handwriting_synthesis` using the E8 DMA
because recurrent state must be transferred between inferences. They then
clarified that small state copies may be faster on the CPU. We agreed to measure
CPU versus DMA on the actual board and select DMA only if it improves performance.
The measurements show the pack DMA path is slower at every tested application
size. The likely next optimization is removing unnecessary copies, but it has
**not been implemented or measured yet**. Do not claim an optimization speedup.

## State when pausing

- Board: Alif DevKit-E8, AE822FA0E5597LS0, M55_HP Cortex-M55 r1p0, Ethos-U85.
- Selected target: `DevKit-E8@Handwriting_Release`.
- Firmware: the baseline application plus an enabled transfer benchmark.
- Build b-49 succeeded, then `restart_debugging` loaded it through CMSIS Load.
- The debugger's initial main breakpoint was continued. The target is **running**.
- The corrected benchmark completed successfully, followed by **Handwriting READY**.
- COM7 at 115200 baud was closed/released after reading the result.
- No custom breakpoints were added. The existing launch main breakpoint is temporary.
- No model, quantization, Vela configuration, PTE, generated graph, imported pack,
  library, or board-layer files were changed. No model reprogramming is needed.
- No commits were made. No subagents were used.

## Uncommitted changes

At pause, `git status --short --ignore-submodules=all` showed exactly:

```text
 M projects/handwriting_synthesis/app_main.cpp
 M projects/handwriting_synthesis/handwriting_synthesis.cproject.yml
?? tests/handwriting/board_transfers.cpp
```

This checkpoint itself is an additional new file.

- `app_main.cpp`: under `HANDWRITING_TRANSFER_BENCHMARK`, declares and calls
  `handwriting_transfer_benchmark(engine)` after the existing startup fixtures.
- `handwriting_synthesis.cproject.yml`: currently **enables**
  `HANDWRITING_TRANSFER_BENCHMARK` and adds `../../tests/handwriting/board_transfers.cpp`
  to the application source group. Disable the define for the final normal demo.
- `tests/handwriting/board_transfers.cpp`: new original Apache-2.0 benchmark,
  entirely compiled out without the define. It initializes/powers DMA1,
  allocates a handle once, tests copies, deallocates, powers off/uninitializes,
  then runs the baseline 64-step synthesis measurement. Source includes
  `cache.h` and `<initializer_list>`; an early compile failed before these were
  added. The corrected source builds successfully.

## Measurements already obtained

Actual board, Release/AC6 6.24.0, OSPI 100 MHz, AXI/timer 400 MHz.
Each transfer row is the **sum of 64 timed copies**, after one warmup.
CPU uses a noinline memcpy wrapper and DSB. DMA uses the existing pack DMA1
driver, BS_BYTE_4, burst length 16, completion interrupt plus bounded polling.
Initialization/power/channel allocation are outside the timed copies; Start,
microcode generation, cache maintenance and completion are inside.
The benchmark cleans destination bytes before DMA; the driver cleans the source
and invalidates the destination before/after transfer. All copies and surrounding
destination guard bytes passed. Offsets reproduce float-packed input slices.

Latest successful run:

| Source → destination | Bytes | Offset | CPU ticks, 64 copies | DMA ticks, 64 copies |
| --- | ---: | ---: | ---: | ---: |
| DTCM → DTCM | 12 | 292 | 11136 | 89520 |
| DTCM → DTCM | 292 | 0 | 17152 | 93328 |
| DTCM → DTCM | 1600 | 304 | 43392 | 135217 |
| DTCM → DTCM | 1600 | 12 | 43392 | 135124 |
| DTCM → DTCM | 1600 | 1612 | 43392 | 135104 |
| DTCM → DTCM | 1600 | 1904 | 43392 | 134603 |
| SRAM → DTCM | 120 | 0 | 13376 | 96336 |
| SRAM → DTCM | 484 | 0 | 20080 | 101775 |
| SRAM → DTCM | 6400 | 0 | 126592 | 287956 |

Thus a 1600-byte copy costs about 1.695 us on CPU and 5.28 us with this DMA
path. A 6400-byte copy costs about 4.945 us on CPU and 11.25 us with DMA.
DMA is approximately 3.1x and 2.3x slower respectively, even with initialization
amortized. This establishes the current driver path, not every possible custom
DMA microcode implementation. There is no useful independent work to overlap
with these immediately consumed copies in the current sequential chain.

Full-step baseline from the **corrected fixture**:

```text
TRANSFER timer_hz=400000000
TRANSFER_STEP samples=64 ticks=859573127 stroke_hash=65bfb6d3 error=0 engine_bytes=20288
Handwriting READY. UART4 115200 8N1; 1..75 characters, Return starts. White on black, 800x480.
```

This is 33.5770752734 ms per warmed complete step, covering five model executions
plus CPU recurrence, attention, sampling and validation. It excludes graph/event
dispatch, UART and LCD. It uses seed 42, one untimed warmup and the exact
75-character sentence:

```text
The quick brown fox jumps over the lazy dog Pack a box with five dozen jugs
```

The source has a static_assert for the sentence length. The first benchmark used
a 77-character sentence and returned -424 before running any steps. Disregard
earlier `TRANSFER_STEP ticks=0 stroke_hash=811c9dc5 error=-424` records: that was
a benchmark-fixture mistake, fixed and rerun, not an application model fault.
The last serial read contained earlier buffered failed-run output followed by
the latest successful run; the values above are from the final run.

Startup verification also passed:

- Model 3,355,648 bytes; CRC `cf5a91ed`.
- CPU recurrence fixture max error 0.000000477, tolerance 0.000003.
- Dense fixtures all passed; errors lstm1 0.001420975, attention 0,
  lstm2 0.001415491, lstm3 0.001286507, gmm 0.001420736.
- Method pool peak 37336 bytes; temporary pool peak 4960 bytes.
- Current Engine size 20288 bytes.

## Code understanding and proposed next implementation

Read these project files first:

- `nodes_impl/handwriting_engine.hpp` and `.cpp`: State, CPU recurrence,
  per-method packing, and ordered `stage(0..4)` execution.
- `nodes_impl/handwriting_runtime.cpp`: `Engine::dense` runtime boundary.
- `nodes_impl/HandwritingSynthesis.hpp` and `HandwritingNetwork.hpp`: five
  separate model nodes serialize on the Stream event thread.
- `../../common/arm_embedded_module.cpp`: `EmbeddedModule::execute` and lifetime.
- `../../common/nodes_impl/c/embedded_runtime.cpp`: planned method/temp pools.
- `linker_ac6.sct.src`: default data/heap in DTCM, pools in SRAM0.
- `../../tests/test_handwriting_networks.cpp`: deterministic dense stub and
  production controller/model-chain tests.

The NPU runs dense gate methods, **not complete LSTM recurrence**. The CPU
computes h/c updates, attention/window, and sampling. State h/c and `input_`
are in DTCM (static startup engine, or controller allocated from the DTCM heap).
They bypass the CPU cache; moving these small copies to DMA adds overhead.

Current dimensions, floats:

```text
method:     lstm1 attention lstm2 lstm3 gmm
input:      476   476       876   876   400
output:     1600  30        1600  1600  121
```

`dense()` executes the method, gets the memory-planned float tensor, memcpy's
the complete tensor into `Engine::output_[1600]`, then checks finiteness.
`stage()` immediately consumes the copied output before the next execution.
The copy totals 19,804 output bytes per stroke. A candidate is a new
`dense_view` boundary returning a const pointer into the method's planned output,
validated and consumed **within the same stage before any next execution**.
Keep the copying `dense()` API for startup fixtures if useful. Confirm tensor
storage lifetime in the local ExecuTorch implementation first; a pointer must
not depend on the destroyed result vector. Planned output storage is expected
to belong to the loaded method. Avoid retaining views across subsequent model
calls. Remove `output_` from Engine only after this is verified and host stubs
are updated. Benchmark: direct reads from SRAM may have a different cost from
copying once into DTCM, so measure rather than assume this is faster.

Also redundant input packing can be removed without changing the model:

- Stage 0 packs `[old window 73, xy 3, old h0 400]`.
- Stage 1 needs `[same old window, same xy, updated h0]`: overwrite only the
  h0 slice at +76; the 76-float prefix is already correct.
- Stage 2 packs `[xy 3, h0 400, new window 73, old h1 400]`.
- Stage 3 needs `[same xy, updated h1 400, same new window, old h2 400]`:
  overwrite h1 at +3 and h2 at +476, retaining xy and window.
- Stage 4 already passes h2 directly; no packing copy.
- Together these remove another 608 bytes per stroke. Keep stage-order guards
  and failure/retry semantics correct. Existing host tests inspect every
  input slice against current state, including old versus new window.

None of these proposed production optimizations is implemented yet.

## Resume steps

1. Read repository/user AGENTS instructions and this checkpoint. Preserve all
   existing user changes. Use the relevant skills as needed: `helium-wiki`,
   `cmsis-pack-docs`, `check-cmsis-environment`, `cmsis-debug-live`.
2. Check CMSIS session status before any target operation. COM7 is free.
3. Implement the measured copy-elimination candidate, adapting host boundary
   stubs and adding meaningful lifetime/error/packing checks as necessary.
4. Run host checks using the repository Python and an installed C++17 compiler:

   ```powershell
   .venv/Scripts/python.exe tests/run_handwriting_tests.py --vcvars <vcvars64.bat>
   .venv/Scripts/python.exe -m unittest discover -s tests -p test_handwriting_graph.py
   ```

   Host tests have **not been run during this session yet**. Installed Visual
   Studio provides vcvars64.bat; resolve its location at runtime. Do not record
   absolute paths to external local projects in repository files.
5. Build through `cmsis_action {action:'build', target:'DevKit-E8@Handwriting_Release'}`.
   Leave the benchmark enabled while comparing. Load through CMSIS tools,
   continue from the launch main breakpoint, capture COM7. Compare step ticks,
   all startup fixtures and the baseline stroke hash `65bfb6d3`. Repeated trials
   are warranted because the expected speedup may be very small versus the
   roughly 33.58 ms step dominated by model weights/OSPI.
6. Keep only changes with a measured benefit (or a clearly documented memory
   reduction); do not force DMA when it is slower. Explain this to the user.
7. Disable `HANDWRITING_TRANSFER_BENCHMARK` for the final normal firmware,
   retain the opt-in benchmark for reproducibility if appropriate. Rebuild,
   load and verify READY, leave target running, release serial port.
8. Update README/validation evidence with appropriately scoped measurements.
   This checkpoint can be updated to reflect completion.

## Tooling and safety details

- Follow AGENTS.md: all board operations, build/load/debug, serial I/O,
  manuals and build-artifact inspection go through CMSIS Developer Assistant
  MCP tools. Do not invoke gdb/pyocd/etc or build firmware from the shell.
- `cmsis_action` uses target `DevKit-E8@Handwriting_Release`. A running build
  is polled with action `status`, never launched again in parallel.
- `.cmsis/tools-environment.yml` passed full installed-schema validation,
  consumed-field/path validation and tool checks. Intended solution matches.
  CMSIS-Toolbox 2.15.0, AC6 6.24.0, CMake 3.31.12 and Ninja 1.13.2 found.
  Process-local checks used paths/variables from that generated file only.
  Re-read/reproduce it if needed. Skill-required environment checks are the
  only shell cbuild commands used; actual builds used cmsis_action.
- First benchmark build b-46 was unconfirmed/failed; after fixing includes,
  b-47 succeeded, and b-49 with the corrected 75-character fixture succeeded.
  `get_build_diagnostics` still displayed the old b-46 diagnostic log after
  a later confirmed successful build; do not mistake it for a new failure.
- A stop_debugging immediately followed by load_and_debug once hit PROBE_BUSY
  during teardown. Polling get_session_status until no-session resolved it.
  `restart_debugging` works and runs CMSIS Load before reset/main breakpoint.
- pause_execution may return before a frame is ready. Wait/check session or
  call stack before evaluate_expression. Existing debug UI background
  evaluate errors appeared in recent problems; no board fault was established.
- Git status/diff without ignoring submodules encounters permission errors
  in the CMSIS-Stream submodule metadata. Use
  `git status --short --ignore-submodules=all` and
  `git -c diff.ignoreSubmodules=all diff`.

## Documentation evidence already read

- `web/alifsemi-com/a927e530c03e`, ADTS0013 v1.2 (June 2026), pp.42–44:
  DMA1 belongs to RTSS-HP; general-purpose memory-to-memory/scatter-gather DMA,
  eight channels, domain/AXI connectivity. Read through documentation tools.
- `arm/101051-latest`, r1p1, Issue 03, p.198, section 10.9: TCM accesses
  cannot be cached (applicable M55 principle; board core itself is r1p0).
- Local wiki `techniques-armv8m-cache-management`:
  `.agents/skills/helium-wiki/wiki/techniques/techniques-armv8m-cache-management.md`.
- Pack `AlifSemiconductor::Ensemble@2.2.1`: read `Driver_DMA.h`, `Driver_DMA.c`,
  bare-metal DMA memcpy example, cache.h and sys_utils.h. No imported files
  were modified or copied. Driver performs LocalToGlobal address conversion,
  generates/cleans microcode per Start, cleans source, invalidates destination,
  invalidates again before the completion callback. Benchmark uses this public
  driver API and initializes DMA1 only before Stream startup.

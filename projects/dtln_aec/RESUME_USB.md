# Resume the DTLN-aec USB demo on a fresh DevKit-E8

Handoff recorded on 2026-11-19. Use the current EthosCatalog working tree,
including the uncommitted SDS source, sink, generated graph, and USB demo changes.
The other board needs both the model programmed into OSPI and the application
loaded into MRAM.

## First-time board setup

1. Use `EthosCatalog.csolution.yml` with the M55_HP core. Set up the
   M55_HP single-core debug stub/ATOC if required on this board. Keep the
   `PRG USB` connection and debug probe connected. Use SW4 at U4 for optional
   UART4 output after the ATOC setup. [DevKit-E8 setup](../../board/DevKit-E8/README.md#setools)
2. Keep the generated `projects/dtln_aec/ai_layer/` artifacts. The current
   `model.pte` is 1,742,288 bytes, CRC32 `31C8406C`, SHA-256
   `AE9EA923AC96FD7536E0B780B1DF73FF9710171881EF91677868A7CABD6A91C7`.
   The model is already compiled for the application. A board change uses
   this same binary.
3. From the repository root, use the existing `out/dtln_aec-model.cbuild-run.yml`
   when it points to this workspace and PTE. It loads the PTE at `0xC3C00000`,
   attaches to M55_HP, and disables both pre- and post-reset. To regenerate it,
   build `DevKit-E8@DTLN_AEC_ReleaseSRAM` in CMSIS and use its application
   `cbuild-run.yml`:

   ```powershell
   uv --cache-dir .uv-cache run --with pyyaml python scripts/create_model_flash.py --project dtln_aec --run out/EthosCatalog+DevKit-E8.cbuild-run.yml --algorithm board/flash/DevKit-E8_IS25WX.FLM
   ```

4. Build and **Load & Debug** `DevKit-E8@DTLN_AEC_ProgramModel`. Break at
   `model_programming_wait`. Verify `model_programming_ready == 1`,
   `app_stage == 5`, `app_error == 0`, and `app_service_error == 0`.
   Detach and free the probe without resetting or power-cycling. Use the
   CMSIS Developer Assistant `flash` tool explicitly with
   `cbuildRunFile: out/dtln_aec-model.cbuild-run.yml`. Wait for programming
   success. [OSPI programming handoff](../../documentation/alif-e8-flashing.md#program-an-ospi-model)
5. Connect the underside USB-C **Device** (`MCU USB`) port to the PC with
   its own data cable. Load and run `DevKit-E8@DTLN_AEC_ReleaseSRAM`,
   wait for `SDSIO-Client` to appear in Windows, then start the PC server.
   Verify `DTLN_Test_result PASS`, model address
   `0x02000000`, `cached_in_sram=1`, and CRC `31C8406C` in the startup
   output, followed by `SDSIO-Client USB interface initialized successfully`.
   [Manual startup order](test_data/README.md#run-the-usb-demo-manually) · [Alif DevKit-E8 User Guide](https://alifsemi.com/download/AUGD0023)

## Replay and compare

The nine original WAVs under `test_data/audio_samples/` and the generated
playback files and references under `test_data/sds_run/` are local,
git-ignored files. They are ready in this workspace. On a different PC,
restore the WAVs from the pinned upstream source and run `sds_demo.py prepare`
using NumPy and TensorFlow 2.20. [Fixture sources and preparation](test_data/README.md)

```powershell
.\projects\dtln_aec\test_demo.ps1
```

Run the launcher in a Windows terminal with access to WinUSB. Keep one
SDSIO-Server instance per Device port. Use CMSIS Developer Assistant for
firmware and debug actions.

Expect three captures in `test_data/sds_run/`:

| Capture | Clip | SDS records | Paired samples after alignment |
| --- | --- | ---: | ---: |
| `Dtln_Out.0.p.sds` | Far-end only | 1,365 | 173,920 |
| `Dtln_Out.1.p.sds` | Near-end only | 1,376 | 175,360 |
| `Dtln_Out.2.p.sds` | Double-talk | 1,340 | 170,720 |

The comparator validates counts, 8 ms timeslots and finite samples. It
aligns each capture to the float DTLN-128 reference and produces RMS, MAE,
RMSE, peak error, and an aligned listening WAV. Use the measured board
results to decide whether to set `--max-mae`. [Comparison details](test_data/README.md#compare-the-captures)

## USB DMA on the DevKit-E8

The Release scatter file places the MDK endpoint-zero buffer and SDS bulk OUT
buffer in `NS_REGION_0` with the Alif USB driver buffers. This lets the USB
controller access the buffers through the global DTCM alias. Before the change,
the controller stalled on a two-byte `GET_STATUS` reply from the endpoint-zero
buffer and the firmware never reached USB configuration.

The Arm Compiler 6 build routes SDS bulk IN sends through one 8 KiB buffer
in the same DMA region. SDS passes headers and payloads from ordinary
application memory, including thread stacks. The wrapper keeps the USB
transport and its completion handling in the SDS pack.

## Earlier SRAM verification

ReleaseSRAM build `b-11` and load `r-12` used the same PTE and Vela
configuration. The firmware verified the OSPI image and the SRAM0 copy,
then both NPU stages passed the independent startup fixtures. Linked
SRAM0 usage was 2,556,400 of 4,194,304 bytes.

The scheduler measured each completed hop from before the SDS source
through the SDS sink, including board-side SDS waits. It measured zero
deadline misses against the 8 ms budget with a 400 MHz timer.
SDSIO-Server completed all steps, and the comparator verified record counts,
timestamps and finite samples. These timings cover on-board processing,
with the host running a file replay.

| Clip | SDS records | Worst hop (ms) | MAE vs. float DTLN-128 |
| --- | ---: | ---: | ---: |
| Far-end only | 1,365 | 2.023 | 0.000128 |
| Near-end only | 1,376 | 2.027 | 0.016893 |
| Double-talk | 1,340 | 2.023 | 0.006542 |

The local `sds_run/` directory holds the ReleaseSRAM captures and aligned
WAVs. The direct-OSPI captures and WAVs are in `sds_run/ospi/`.

## Direct-OSPI verification

On 2026-11-23, Release build `b-9` and load `r-10` verified CRC `5AE153B6`
at model address `0xC3C00000` with `cached_in_sram=0`. Both NPU methods
passed the startup fixtures. Their measured median adapter times were
23.641 ms and 39.406 ms per hop.

SDSIO-Server completed all three playback steps and exited successfully.
The board returned zero playback and close errors for each clip. The
comparator verified the record counts and timestamps and wrote the aligned
capture WAVs. MAE is measured against the float DTLN-128 output.

| Clip | SDS records | MAE |
| --- | ---: | ---: |
| Far-end only | 1,365 | 0.000127 |
| Near-end only | 1,376 | 0.016896 |
| Double-talk | 1,340 | 0.006558 |

## Previous board state at the pause

- ReleaseSRAM build `b-21` passed, and `r-22` loaded that firmware onto the
  previous board. It retries USB startup after a timeout, cleaning up the
  SDS 3.1 USB client and disconnecting before each retry.
- All three input files passed the stock SDS file checker. The production
  graph passed the host test with these clips, using doubles for SDS I/O
  and inference. The stock SDSIO file handler and comparator also passed
  a simulated capture round-trip. Hardware capture and numerical board
  results are pending.
- On the previous board, Windows enumerated `SDSIO-Client` with WinUSB
  (VID `C251`, PID `8007`), but libusb reported a control endpoint stall
  while reading the USB product name. The target reported a high-speed link
  with `usbd_dev_ptr[0]->data_ptr->configuration == 0` and
  `sdsInitialized == 0`. A power cycle and a Device-only replug produced
  no SDS captures. The same symptoms reappeared on the new board before the
  DMA buffer placement changes described above.
- SDSIO-Server was stopped for the pause. The previous target was left running.

# DTLN-aec recordings for SDS

The nine original WAV files are stored locally in `audio_samples/`. All are
mono, 16 kHz, signed 16-bit PCM. Each file name consists of the stem below and
one of the three suffixes.

| Test case | Filename stem | `_mic.wav` samples | `_lpb.wav` samples | `_processed.wav` samples |
| --- | --- | ---: | ---: | ---: |
| Far-end speech only | `9mkQhVtzTEy2hDk-6u2Sww_farend_singletalk` | 174080 | 173920 | 173920 |
| Near-end speech only | `DLhjtuwiEkS-68TsUVvW5g_nearend_singletalk` | 175360 | 175658 | 175360 |
| Both ends speaking | `DMTgmZwtgUilp4omPK7-OQ_doubletalk` | 172160 | 170720 | 170720 |

## Prepare the inputs

From the repository root, run the following with a Python environment that
has NumPy and TensorFlow 2.20. This creates `Dtln_In.0.sds`, `Dtln_In.1.sds`,
and `Dtln_In.2.sds` in the local `sds_run/` directory. It also generates
sample-aligned references using the [bundled float DTLN-aec 128 models](../model/upstream/).

```powershell
.venv/Scripts/python.exe projects/dtln_aec/test_data/sds_demo.py prepare
```

Each input pairs the microphone and loopback on the sample index and uses the
shorter WAV length. It packs little-endian float32 values in microphone-then-
loopback order, one 128-sample hop per record. Timeslots start at 0 and
increment by 8 ms. The tool prefixes 384 zero samples for model warm-up and
pads the tail to a full hop plus 384 zeros to drain overlap-add.

## Run the USB demo manually

1. Make sure the input files are in `test_data/sds_run/`. Prepare them as
   above if needed. On a fresh board, program the model once and build
   `DevKit-E8@DTLN_AEC_ReleaseSRAM`. [Model programming](../README.md#model-programming)
2. Connect the standard LCD to J21 with power off. Keep the programming
   connection on `PRG USB` and connect a second USB-C data cable from the
   underside `MCU USB` Device port to the PC. [Alif DevKit-E8 User Guide](https://alifsemi.com/download/AUGD0023)
3. Start the board demo. In the CMSIS view, select
   `DevKit-E8@DTLN_AEC_ReleaseSRAM` and use **Load & Run**. If the
   firmware is already installed, power on or reset the board. Wait for
   **Load & Run** to finish. Open **Device Manager** from the `Win + X`
   menu and expand **Universal Serial Bus devices**. Look for
   **SDSIO-Client**. You can also check from PowerShell:

   ```powershell
   pnputil /enum-devices /connected /deviceid 'USB\VID_C251&PID_8007'
   ```

   The listing should show `Device Description: SDSIO-Client` and
   `Status: Started`.
4. From the repository root in Windows PowerShell, start SDSIO-Server.
   Use one server instance per device.

   ```powershell
   .\projects\dtln_aec\test_demo.ps1
   ```

**Board first is the reliable order when using Load & Run.** The server
can also wait for a single boot of already installed firmware. During
**Load & Run**, an additional programming reset can interrupt a replay
that has already started. If that happens, let the board finish booting
and start the server again to capture all three clips.

The server plays the three configured clips automatically and exits after
the last one. The LCD labels the aligned input and output traces
`MICROPHONE` and `ECHO CANCELLED`. The server writes
`Dtln_Out.0.p.sds`, `Dtln_Out.1.p.sds`, and `Dtln_Out.2.p.sds` into
`sds_run/`. Move any captures you want to keep before starting a replay.
[ARM SDS USB transport](https://arm-software.github.io/SDS-Framework/main/sdsio.html#layer-sdsio_usb)

## Compare the captures

The test launcher prints the comparison automatically. To recheck saved captures, run:

```powershell
.venv/Scripts/python.exe projects/dtln_aec/test_data/sds_demo.py compare --summary
```

The comparison checks the record counts, 8 ms timeslots, record sizes and
finite values. It then removes the 384-sample warm-up prefix and 384-sample
graph delay and trims to the original paired length. The table reports
sample-wise MAE, RMSE and peak absolute error against the original float
DTLN-aec 128 output. It also shows the RMS level change from the input
microphone for both outputs. A negative value means a quieter output.
For the far-end-only clip, this level drop measures echo suppression.
Use the synthetic test below to measure double-talk error against clean
near-end speech.

The script writes aligned `*.capture.wav` files for listening alongside
`*.reference.wav`. Omit `--summary` for JSON with the raw RMS values. Add
`--case far-end` for one clip or `--max-mae <value>` to enforce an error limit.
The upstream `*_processed.wav` files provide a listening reference from
the larger DTLN-aec 512 model.

## Measure echo cancellation with a known clean signal

`validate_patterns.py` reuses the deterministic five-second pattern and
concatenates six repeats into one USB stream. Each repeat contains silence,
echo only, double-talk, near-end only, and silence. The firmware and the
original 128-unit float TFLite reference both carry recurrent state across all
six repeats.

```powershell
.venv/Scripts/python.exe projects/dtln_aec/test_data/validate_patterns.py prepare
.\projects\dtln_aec\test_demo.ps1 -Synthetic
```

The comparison checks SDS record counts and timestamps and aligns the
captured output with the reference. It scores the center 500 ms of each
one-second section. Echo-only ERLE compares input echo power with output
power. Double-talk error reduction compares the input and output error
power relative to the known clean near-end signal. Near-end gain compares
output RMS with clean-signal RMS, with 0 dB corresponding to the clean
signal level. The per-repeat values and aligned WAVs are in
`sds_run/synthetic/`. [Board measurements](../VALIDATION.md#usb-signal-processing-results)

The check allows up to 2 dB difference in near-end gain from the original.
Echo-only ERLE and double-talk error reduction must each be within 1 dB of
the original or better in every repeat.

## Source and recovery

Restore the files by downloading the nine WAVs from the pinned upstream
directory into `audio_samples/`, keeping their original file names. The local
copies were verified against the Git blob hashes at that commit.
[DTLN-aec audio samples at commit `9d24e128`](https://github.com/breizhn/DTLN-aec/tree/9d24e128b4f409db18227b8babb343016625921f/audio_samples)

The microphone and loopback names match Microsoft's ICASSP 2021 AEC Challenge
blind test set. The published terms leave redistribution rights for these
specific recordings unclear, so the WAV directory is git-ignored. The
DTLN-aec code license is preserved with the bundled upstream files.
[Microsoft source recordings](https://github.com/microsoft/AEC-Challenge/tree/main/datasets/blind_test_set/noisy) · [Dataset terms](https://github.com/microsoft/AEC-Challenge#dataset-licenses) · [DTLN-aec license](../model/upstream/LICENSE)

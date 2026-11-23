# DTLN-aec recordings for SDS

The recorded-speech demo uses six upstream WAVs, one microphone and one
loopback file per case. The downloader stores them locally in `audio_samples/`.
They are mono, 16 kHz, signed 16-bit PCM. The table also lists the optional
upstream `_processed.wav` listening references.

| Test case | Filename stem | `_mic.wav` samples | `_lpb.wav` samples | `_processed.wav` samples |
| --- | --- | ---: | ---: | ---: |
| Far-end speech only | `9mkQhVtzTEy2hDk-6u2Sww_farend_singletalk` | 174080 | 173920 | 173920 |
| Near-end speech only | `DLhjtuwiEkS-68TsUVvW5g_nearend_singletalk` | 175360 | 175658 | 175360 |
| Both ends speaking | `DMTgmZwtgUilp4omPK7-OQ_doubletalk` | 172160 | 170720 | 170720 |

## Prepare the inputs

Before running the recorded-speech demo, run both commands from the repository
root. The first downloads the six required WAVs to
`projects/dtln_aec/test_data/audio_samples/`. The second requires NumPy and
TensorFlow 2.20 and creates `Dtln_In.0.sds`, `Dtln_In.1.sds`, and
`Dtln_In.2.sds` in `sds_run/`. It also generates sample-aligned references
using the bundled float DTLN-aec 128 models. [Float models](../model/upstream/)

```powershell
.venv/Scripts/python.exe projects/dtln_aec/test_data/download_audio_samples.py
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

## Example test output

This is the output of `test_demo.ps1` for the three recorded clips.
`<repo>` stands for the local EthosCatalog checkout.

```text
SDSIO-Server v3.1.0
Press 'Ctrl+C' or 'X' to exit.
Working directory: <repo>\projects\dtln_aec\test_data\sds_run
SDSIO configuration YAML: <repo>\projects\dtln_aec\test_data\datatest.sdsio.yml
SDSIO command input: R=Record, P=playback, S/s=stop, T/t=reset, X/x=exit, A-H=set flags 0-7, a-h=clear flags 0-7.
SDSIO-Client USB device connected.
sdsFlags = 0xB0000000.
Playback step 0: far-end.
Playback: Dtln_In (Dtln_In.0.sds)
Record:   Dtln_Out (Dtln_Out.0.p.sds)
Closed:   Dtln_Out (Dtln_Out.0.p.sds)
Closed:   Dtln_In (Dtln_In.0.sds)
sdsFlags = 0x30000000.
sdsFlags = 0xB0000000.
Playback step 1: near-end.
Playback: Dtln_In (Dtln_In.1.sds)
Record:   Dtln_Out (Dtln_Out.1.p.sds)
Closed:   Dtln_Out (Dtln_Out.1.p.sds)
Closed:   Dtln_In (Dtln_In.1.sds)
sdsFlags = 0x30000000.
sdsFlags = 0xB0000000.
Playback step 2: double-talk.
Playback: Dtln_In (Dtln_In.2.sds)
Record:   Dtln_Out (Dtln_Out.2.p.sds)
Closed:   Dtln_Out (Dtln_Out.2.p.sds)
Closed:   Dtln_In (Dtln_In.2.sds)
Playback complete - no more steps remaining.
SDSIO-Server terminating (playback complete).
sdsFlags = 0x30000000.
SDSIO-Server shut down.
Reference: original float DTLN-aec 128 (TFLite)
dB columns show RMS level change from the input microphone.
Clip          Board dB  Float dB         MAE        RMSE  Peak error
far-end         -49.60    -49.40  3.4341e-05  4.6038e-05  0.00045599
near-end         -0.23     -0.07   0.0015696   0.0084066     0.44779
double-talk      -0.77     -0.70   0.0006322   0.0031717     0.35928
```

`SDSIO-Client USB device connected` confirms that the PC server reached the
board over USB. The startup banner lists the interactive shortcuts.
`test_demo.ps1` starts playback and closes the server automatically.

For each clip, `Playback` names the input sent to the board, and `Record`
names the file receiving the echo-cancelled audio. The number in each file
matches the playback step. The `Closed` lines mark the end of those streams.
`0xB0000000` indicates the server is alive in playback mode with the Start
flag set. `0x30000000` indicates Start has cleared while the server is still
alive in playback mode. After shutdown, the launcher checks for three fresh
captures and runs the Python comparison.

The `Board dB` and `Float dB` columns show each output's RMS level change
relative to the input microphone. A negative value means a quieter output.
The last three columns compare aligned board and reference samples in linear
amplitude units. MAE is the mean absolute difference, RMSE weights larger
differences more heavily, and peak error is the largest absolute difference
in the clip. Smaller errors indicate closer agreement with the original model.

- **Far-end** has far-end speech only, so the level drop estimates echo
  suppression. The board achieves 49.60 dB, within 0.20 dB of the float model.
- **Near-end** has local speech only. The board reduces the overall speech
  level by 0.23 dB versus 0.07 dB for the reference.
- **Double-talk** contains both speakers. The board output is 0.07 dB
  quieter than the reference, with an RMSE of 0.00317.
  [Measure double-talk error against clean speech](#measure-echo-cancellation-with-a-known-clean-signal)

## Compare the captures

The test launcher prints the comparison automatically. To recheck saved
captures, run this command.

```powershell
.venv/Scripts/python.exe projects/dtln_aec/test_data/sds_demo.py compare --summary
```

The comparison checks the record counts, 8 ms timeslots, record sizes and
finite values. It then removes the 384-sample warm-up prefix and 384-sample
graph delay and trims to the original paired length.

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

The downloader fetches the required inputs from the pinned upstream revision
and verifies their SHA-256 checksums. Rerun it to restore missing files or
verify existing copies. The optional `_processed.wav` listening references
are available from the same upstream directory.
[DTLN-aec audio samples at commit `9d24e128`](https://github.com/breizhn/DTLN-aec/tree/9d24e128b4f409db18227b8babb343016625921f/audio_samples)

The microphone and loopback names match Microsoft's ICASSP 2021 AEC Challenge
blind test set. The published terms leave redistribution rights for these
specific recordings unclear, so the WAV directory is git-ignored. The
DTLN-aec code license is preserved with the bundled upstream files.
[Microsoft source recordings](https://github.com/microsoft/AEC-Challenge/tree/main/datasets/blind_test_set/noisy) · [Dataset terms](https://github.com/microsoft/AEC-Challenge#dataset-licenses) · [DTLN-aec license](../model/upstream/LICENSE)

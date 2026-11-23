---
description: DTLN-aec 128 echo cancellation on Alif DevKit-E8 with USB SDS replay and capture.
category: audio
catalog: true
---

# DTLN-aec on DevKit-E8

The demo replays three test clips over USB, captures the echo-cancelled audio
on the PC, and shows the microphone and echo-cancelled waveforms together on
the LCD.

## Run the demo on Windows

Make sure `uv` is on your PATH and the **ARM SDS 3.1.0** pack is installed
in CMSIS Solution. [Prepare the test clips](test_data/README.md#prepare-the-inputs) once.
For a fresh board, [program the model](#model-programming) first.

1. With the board powered off, connect the LCD to J21. Connect the PC to
   **PRG USB** and to the underside **MCU USB Device** port.
2. In VS Code, select `DevKit-E8@DTLN_AEC_ReleaseSRAM` in the CMSIS view and
   click **Load & Run**. When it finishes, open **Device Manager** from the
   **Win + X** menu and look for **SDSIO-Client** under
   **Universal Serial Bus devices**.
3. Once **SDSIO-Client** appears, open PowerShell in the EthosCatalog
   repository root and run the launcher.

   ```powershell
   .\projects\dtln_aec\test_demo.ps1
   ```

The script plays all three clips, saves the captures in
`projects/dtln_aec/test_data/sds_run/`, and prints the comparison with the
original float DTLN-aec 128 model.
During playback, the board releases one audio hop every 8 ms and displays the
**MICROPHONE** and **ECHO CANCELLED** waveforms on the LCD.

[USB checks](test_data/README.md#run-the-usb-demo-manually) · [How to read the metrics](test_data/README.md#compare-the-captures)

## Model programming

Program the model into OSPI once on a fresh board or whenever the model
changes. The `ReleaseSRAM` build copies it into SRAM at startup for real-time
processing. [Fresh-board setup](RESUME_USB.md#first-time-board-setup)

## Validation

With the LCD active, all 7,837 measured hops met the 8 ms deadline. The
slowest took 4.456 ms of processing time. [Board and signal processing results](VALIDATION.md)

[Conversion report](model/CONVERSION.md) · [Benchmarks](model/BENCHMARK.md) · [Application graph](generated/app.pdf)

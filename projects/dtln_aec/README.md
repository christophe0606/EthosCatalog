---
description: DTLN-aec 128 echo cancellation on Alif DevKit-E8 with USB SDS replay and capture.
category: audio
catalog: true
---

# DTLN-aec on DevKit-E8

The demo replays three test clips over USB, plays the echo-cancelled audio
through the DevKit-E8 Audio Out jack, and captures it on the PC. The LCD
shows the microphone and echo-cancelled waveforms together.

## Run the demo on Windows

Make sure `uv` is on your PATH and the **ARM SDS 3.1.0** pack is installed
in CMSIS Solution. Before the first run, download the test WAVs and generate
the SDS replay files from the repository root. The preparation step uses
NumPy and TensorFlow 2.20. [Test clip setup](test_data/README.md#prepare-the-inputs)

```powershell
.venv/Scripts/python.exe projects/dtln_aec/test_data/download_audio_samples.py
.venv/Scripts/python.exe projects/dtln_aec/test_data/sds_demo.py prepare
```

The downloader puts the six WAVs in `projects/dtln_aec/test_data/audio_samples/`.
For a fresh board, program the model first. [Model programming](#model-programming)

1. With the board powered off, connect the LCD to J21 and a powered speaker
   to the green **Audio Out** jack. Connect the PC to **PRG USB** and to the
   underside **MCU USB Device** port.
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
The CMSIS Stream graph sends processed hops to the SDS sink, LCD and speaker
nodes. The LCD node also receives the microphone hop for the paired display.
The speaker starts once four output hops (32 ms of audio) are ready. The
speaker queue holds up to 96 ms, including the block being played, plus one
8 ms hop waiting to form the next block. The hardware FIFO adds about 1 ms.

[USB checks](test_data/README.md#run-the-usb-demo-manually) · [Example test output and metrics](test_data/README.md#example-test-output)

## Model programming

Program the model into OSPI once on a fresh board or whenever the model
changes. The `ReleaseSRAM` build copies it into SRAM at startup for real-time
processing. [Fresh-board setup](RESUME_USB.md#first-time-board-setup)

## Validation

In the LCD validation run before speaker playback was added, all 7,837
measured hops met the 8 ms deadline. The slowest took 4.456 ms of processing
time. [Board and signal processing results](VALIDATION.md)

[Conversion report](model/CONVERSION.md) · [Benchmarks](model/BENCHMARK.md) · [Application graph](generated/app.pdf)

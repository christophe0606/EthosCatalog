---
category: audio
description: EnCodec neural audio reconstruction on the Alif DevKit-E8.
catalog: false
ci:
  mode: metadata_only
  reason: The official checkpoint is supplied locally. CI does not have the checkpoint-derived model cache.
---

# EnCodec on DevKit-E8

This port targets the upstream causal 24 kHz mono EnCodec model. The demo will
replay audio over SDS USB, display the input waveform in landscape mode and
record the decoded samples over the same USB connection.

The encoder consumes a 2,880-sample PCM block and produces 72 discrete codes.
The decoder consumes those codes in codebook-major order and reconstructs a
PCM block. Each direction carries its own state across blocks. The demo
connects the two Stream nodes through a codes FIFO on one E8.

## Reproduce the model build

The port uses the upstream 6 kb/s configuration and carries the codec
state across blocks of 2,880 samples, or 120 ms. Download the official 24 kHz
checkpoint and pass its path to the quantizer. The model loader verifies the
checksum. [Official checkpoint](https://dl.fbaipublicfiles.com/encodec/v0/encodec_24khz-d7cc33bc.th) · [Conversion results](model/CONVERSION.md)

```powershell
.venv/Scripts/python.exe projects/encodec/model/quantize.py --checkpoint <path-to-checkpoint>
.venv/Scripts/python.exe scripts/create_ai_layer.py --project encodec
.venv/Scripts/python.exe projects/encodec/make_staging_manifest.py
```

`EnCodec_ProgramModel` prepares the OSPI controller for programming.
`EnCodec_Release` times all ten exported methods and the serial loopback. It
also runs 100 encoder-only blocks, saves their codes, then runs 100 decoder-only
blocks using those codes. On the DevKit-E8, the slowest encoder block took
119.23 ms and the slowest decoder block took 91.58 ms. Both stages met the
120 ms deadline on all 100 blocks. The serial loopback peaked at 210.78 ms.
[Endpoint benchmark evidence](model/benchmark_endpoints_evidence.json)

The quantizer reorders the calibrated encoder downsampler and the decoder's
first upsampler.
The codec stages the four recurrent delegates and one decoder delegate in SRAM1,
and the two encoder-front groups and a codebook projection in SRAM0. FlatBuffer
metadata sits in DTCM and the remaining delegates run from OSPI.

## Replay and compare audio over SDS USB

Run `DevKit-E8@EnCodec_Demo` with the matching model programmed into OSPI.
Connect the DevKit-E8 MCU USB Device port to the host with a data cable.
Windows enumerates it as **SDSIO-Client**. [Model programming](../../documentation/alif-e8-flashing.md)

Run this command in PowerShell from the repository root. Set
`CMSIS_PACK_ROOT` to your CMSIS pack directory if it is unset. The
[EnCodec host script](test_data/sds_demo.py) uses SDSIO Server 3.1.0 from
that pack to replay a three-second test pattern and capture the decoded
audio. It compares the capture against the original and prints SNR, RMSE,
mean absolute error and peak absolute error.

```powershell
.venv/Scripts/python.exe projects/encodec/test_data/sds_demo.py run --sds-server "$env:CMSIS_PACK_ROOT/ARM/SDS/3.1.0/utilities/sdsio-server.py"
```

If you already have `sdsio-server` on PATH, omit `--sds-server`. When using
the pack's Python script, install its host dependencies in the repository
environment once with `uv pip install --python .venv/Scripts/python.exe ifaddr libusb1 pyserial pyyaml`.

Each invocation prints its output directory under
`projects/encodec/test_data/sds_run/` and saves `Encodec_In.0.sds`,
`Encodec_Out.0.p.sds` and `fixture.json` there. The comparison validates
record sizes, count and timeslots before computing the reconstruction error
against the original input. Each record holds 2,880 samples at 24 kHz,
one 120 ms inference block. Pass `--input-wav <path>` to replay a 24 kHz mono
PCM16 WAV or `--delay-samples <n>` to account for a measured sample offset.

The encoder starts after collecting one 120 ms input block. Each 16 KiB SDS
ring can queue one complete audio record. The graph hands each decoded block
to SDS as soon as it is available. Codec execution and USB scheduling add to
the block collection and queueing latency.

Two consecutive USB replays each captured all 25 records and submitted
26 landscape waveform frames without drops. The captures matched byte for byte.
Against the same original input, the pretrained float codec measured
10.950 dB SNR and the board capture measured 10.624 dB SNR.
Across both replays, the slowest
encoder and decoder blocks took 118.71 ms and 92.14 ms, with zero misses of
their individual 120 ms deadlines. On one board, the serial codec work
took up to 210.50 ms per block.
[Board replay evidence](model/benchmark_sds_usb_evidence.json)

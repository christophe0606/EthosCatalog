# EnCodec checkpoint

The project builds against the pinned ExecuTorch runtime using the official
24 kHz EnCodec checkpoint. Generated checkpoint-derived files live in the
local `model/cache` and `ai_layer` directories. The reproduction steps in the
[README](README.md) regenerate those files for firmware builds.

The DevKit-E8 tested the encoder and decoder separately on 100 blocks per
stage. The decoder used the compressed codes from the encoder run. The
slowest encoder and decoder blocks took 119.23 ms and 91.58 ms. Both stages
met the 120 ms deadline on every block. Serial loopback peaked at 210.78 ms.
[Endpoint benchmark evidence](model/benchmark_endpoints_evidence.json)

The final USB demo captured all 72,000 samples in two consecutive runs. The
captures matched byte for byte and measured 10.62 dB SNR against the input.
Each run submitted 26 landscape waveform frames without drops. The demo is
running on the DevKit-E8 with OSPI model CRC `0x6abf7173`.
[Board replay evidence](model/benchmark_sds_usb_evidence.json)

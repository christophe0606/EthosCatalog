# EnCodec model conversion

The port uses Meta's causal 24 kHz mono checkpoint at 6 kb/s. It preserves
the pretrained encoder, decoder and first eight residual vector codebooks.
The official checkpoint is loaded from a local file and verified by SHA-256.
[Upstream source and model](https://github.com/facebookresearch/encodec)

Streaming calls take 2,880 mono PCM samples, equivalent to nine 320-sample
latent frames and 120 ms of input. The latent-rate kernel-7 convolutions use
reflection at clip start and require at least seven frames to match the
whole-clip model. Later calls carry convolution history and LSTM hidden and
cell state. Each decoder upsampling layer evaluates the trained transposed
convolution as output phases over the previous and current input frame.

The float test compares 12 consecutive blocks with the upstream inference
modules and verifies that reset discards stale state. All 864 codec indices
matched. The maximum absolute errors were `4.10e-5` for the encoder latent
and `1.31e-6` for PCM measured in normalized sample units. Splitting the float
inference produced identical codes and samples to the unsplit streaming model.

## Quantization and lowering

PT2E calibrates INT16 activations and per-channel INT8 weights at convolution
and linear operators. Recurrent nonlinearities and state, codebook lookup and
the remaining tensor operations use the portable runtime. The initial
whole-method export was 49,797,008 bytes with 74 Ethos-U delegates. It produced
nine separately stored copies of each recurrent hidden projection and would
repeatedly fetch those weights over OSPI.

The staged export keeps each hidden projection in one method and batches both
input projections across the nine frames. The firmware invokes the hidden
steps once per frame and can retain the recurrent delegate payloads in SRAM.
Each direction keeps four independent states for its front method, the two
hidden steps and its back method.

The first staged export attempt passed the same tensor object for both
back-method feature arguments. `torch.export` traced that alias, causing the
back graph to ignore one runtime argument and produce incorrect results. The
export now uses distinct tensors for those inputs.

On a held-out 12-block synthetic test, the staged PT2E codec matched 547 of
864 float codec indices. It measured PCM MAE 0.01883, maximum absolute error
0.26629 and SNR 9.32 dB relative to the float codec. In the unsplit comparison,
quantizing only decoder arithmetic while feeding float encoder codes measured
28.11 dB. Keeping the float decoder and changing only the encoder codes
measured 9.22 dB. These synthetic results characterize conversion error on
the test clip. Board timing and audio capture are independent checks.

## Serialized plan

The corrected PTE contains 19,651,680 bytes and 46 Ethos-U delegates.
The four hidden-step methods each contain one extractable delegate. Their
combined payload size is 3,756,144 bytes, occupying most of the 4 MiB SRAM
bank intended for recurrent staging.

| Method | Planned tensor bytes | Peak delegate scratch bytes |
| --- | ---: | ---: |
| `encode_front` | 1,122,864 | 1,777,840 |
| `encode_hidden0` | 26,624 | 5,120 |
| `encode_project1` | 110,592 | 46,080 |
| `encode_hidden1` | 26,624 | 5,120 |
| `encode_back` | 95,520 | 30,720 |
| `decode_front` | 132,096 | 46,080 |
| `decode_hidden0` | 26,624 | 5,120 |
| `decode_project1` | 110,592 | 46,080 |
| `decode_hidden1` | 26,624 | 5,120 |
| `decode_back` | 1,115,152 | 557,520 |

The pinned ExecuTorch pack has components for every serialized CPU
fallback operator. Vela estimates substantial off-chip weight rereads for
some nonrecurrent delegates. The largest printed estimate is 867.16 MB
per call for an 18.9 million MAC delegate. These estimates are separate
from serialized payload size. Measure actual latency with OSPI and SRAM
staging enabled.

The Vela `Size` build reduces peak delegate scratch to 552,960 bytes.
The demo keeps four recurrent payloads and one expensive encoder convolution
in SRAM1 and the largest encoder delegate in SRAM0. The USB graph passes
the encoder’s 72 code indices to a separately timed decoder node.

An experimental export put `encode_front` into one delegate and
`decode_back` into a separate delegate. The 3.40 MB `encode_front` payload
ran from OSPI and spent 371.83 ms in the NPU on the first block. The encoder
completed that block in 502.89 ms. The run produced no decoded records.
The working demo uses the smaller staged delegates. [Board experiment](benchmark_convolution_fusion_evidence.json)

## Banded A16W8 board experiment

Leaving five ELU operations between convolution groups produced 27 delegates
with the same ten method signatures. The benchmark staged two encoder-front
payloads, one decoder-front payload, two decoder-back payloads and the four
recurrent payloads. It completed all twelve synthetic blocks.

| Layout | Worst encoder | Worst decoder | Worst loopback | Active NPU per block |
| --- | ---: | ---: | ---: | ---: |
| Split Size | 194.85 ms | 188.26 ms | 383.08 ms | 156.14 ms |
| Banded A16W8 | 253.99 ms | 110.66 ms | 364.51 ms | 266.79 ms |

The banded decoder stayed within the 120 ms period on this fixture. The
banded encoder's third delegate used 149.64 ms of NPU time with its 2.68 MB
weights in OSPI. All combined blocks missed the deadline. The last decoded
block measured 8.09 dB SNR against the banded pre-Vela output. The verified
split Size demo was the USB baseline for the next experiment. [Per-method records](benchmark.yml) · [Board evidence](benchmark_banded_a16_evidence.json)

## Packed A16W8 board experiment

After calibration, the encoder groups eight adjacent time positions into
channels. The original stride-8 convolution becomes a stride-1 convolution
with two time taps. The decoder packs two adjacent latent frames into the
existing first upsampler. Each rearrangement preserves its calibration scales,
integer coefficients, streaming state interface, and position in the codec.

The packed model completed the same twelve synthetic blocks in at most
212.63 ms per block. The encoder took at most 124.17 ms and the decoder took
88.53 ms. The encoder's third delegate fell from 149.64 ms to 33.42 ms of
active NPU time. Total active NPU time was 142.24 ms per 120 ms audio block.
All twelve blocks missed the deadline. [Per-method records](benchmark.yml) · [Packed board evidence](benchmark_packed_a16w8_evidence.json)

Two USB replays of the packed model each captured all 72,000 samples and
matched byte for byte. SNR was 10.62 dB against the input and 11.70 dB against
the pretrained float reconstruction. The split Size capture measured 10.02 dB
and 13.01 dB on the same comparisons. [USB replay evidence](benchmark_sds_usb_evidence.json)

## Independent endpoint timing

The regenerated packed PTE was measured with the larger encoder codebook
projection staged in SRAM0 and the MVE codebook argmax. The DevKit-E8 encoded
100 blocks in a standalone pass, then decoded the saved codes in a second
pass. The worst encoder and decoder times were 119.23 ms and 91.58 ms. Both
met the 120 ms deadline on every block. The same model reproduced the packed
USB capture byte for byte across two replays. [Endpoint benchmark evidence](benchmark_endpoints_evidence.json) · [USB replay evidence](benchmark_sds_usb_evidence.json)

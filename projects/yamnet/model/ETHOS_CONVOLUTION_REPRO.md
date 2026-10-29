# Reopening the YAMNet Ethos convolution investigation

The fully delegated quantized graph **stalls on the board**. The exact failing
operator has not been isolated. Queue read positions near a depthwise command
are observations, not proof that that command caused the stall: the NPU
pipelines commands. Neither the first convolution nor a particular depthwise
shape can yet be called a proven hardware/compiler defect.

[HARDWARE_INVESTIGATION.md](HARDWARE_INVESTIGATION.md) records each build/session,
PTE hash, storage/quantization/scheduling variant and observed queue state.
The workaround excludes the first convolution and all depthwise blocks from
quantization, leaving those operations as CPU float kernels. Pointwise/head
operations remain automatically partitioned onto Ethos (14 delegates).

The original failing fully delegated policy used symmetric int16 activations
and per-output-channel int8 weights (a16w8); the tested int8 variations are
listed below. The working hybrid CPU operations register as
`aten::convolution.out`, with depthwise expressed through `groups`; explicit
padding registers as `aten::constant_pad_nd.out`, followed by ReLU. The
fully delegated graph is represented by an Ethos delegate at runtime, so a
queue offset cannot be mapped to an isolated ATen failure without retaining
and analyzing its compiled command schedule.

## Exact logical convolution shapes

These are the reconstructed model's **NCHW logical shapes**, before compiler
layout conversion, fusion, tiling or quantization. Input is `[1,96,64]` log mel,
reshaped to `[1,1,96,64]`. Every row uses dilation 1 and a 3x3 kernel. Explicit
TensorFlow SAME padding is applied before convolution; the convolution itself
uses padding 0. The padded input shape below is therefore the actual logical
convolution input. Weights are OIHW; biases have one element per output channel.
These rows identify the CPU fallback scope and candidate isolated reproducers;
they are **not individual failing-operator tests**.

| Model block | Operator | Unpadded input | Padded convolution input | Weight | Output | Stride | Groups |
|---|---|---|---|---|---|---|---|
| layer1 | Conv2D | 1x1x96x64 | 1x1x97x65 | 32x1x3x3 | 1x32x48x32 | 2 | 1 |
| layer2 | DepthwiseConv2D | 1x32x48x32 | 1x32x50x34 | 32x1x3x3 | 1x32x48x32 | 1 | 32 |
| layer3 | DepthwiseConv2D | 1x64x48x32 | 1x64x49x33 | 64x1x3x3 | 1x64x24x16 | 2 | 64 |
| layer4 | DepthwiseConv2D | 1x128x24x16 | 1x128x26x18 | 128x1x3x3 | 1x128x24x16 | 1 | 128 |
| layer5 | DepthwiseConv2D | 1x128x24x16 | 1x128x25x17 | 128x1x3x3 | 1x128x12x8 | 2 | 128 |
| layer6 | DepthwiseConv2D | 1x256x12x8 | 1x256x14x10 | 256x1x3x3 | 1x256x12x8 | 1 | 256 |
| layer7 | DepthwiseConv2D | 1x256x12x8 | 1x256x13x9 | 256x1x3x3 | 1x256x6x4 | 2 | 256 |
| layer8–12 (five blocks) | DepthwiseConv2D | 1x512x6x4 | 1x512x8x6 | 512x1x3x3 | 1x512x6x4 | 1 | 512 |
| layer13 | DepthwiseConv2D | 1x512x6x4 | 1x512x7x5 | 512x1x3x3 | 1x512x3x2 | 2 | 512 |
| layer14 | DepthwiseConv2D | 1x1024x3x2 | 1x1024x5x4 | 1024x1x3x3 | 1x1024x3x2 | 1 | 1024 |

Stride-two padding is asymmetric: bottom/right receive the extra element.
Source: [model.py](model.py), `LAYER_DEFS` and `ConvRelu.forward`; the pinned
[upstream model](upstream/yamnet.py) is retained with its Apache license. Ethos
activation layouts are produced by the compiler; do not confuse this logical
NCHW table with packed NPU tensor/weight addresses.

## Strongest observed failures and controls

All-NPU a16w8: direct OSPI at 100 MHz, valid whole-model CRC, matching Release
symbols, stable debugger session and a launch gate. Build/session b59/d60:
zero completed invocations after more than 20 s; STATUS=1,
QREAD=1060/QSIZE=10012. Routing QCONFIG=2, REGIONCFG=0x13,
MEM_ATTR2/3=0x24 matched the working direct-OSPI control. Original PTE hash:
`4d4b522d1d7b3a8a28fddd5b93c648499d8a48d77b57214640c2da455cbeffb3`.

Changing just the first convolution to int8 did not resolve the stall
(b63/d64, QREAD=1364/QSIZE=10316). A mixed int8 depthwise graph also stalled
(b41/d43, QREAD=1364/QSIZE=12720), and had unacceptable host fidelity.
Equivalent dense diagonal replacements for depthwise operations stalled too
(b49/d51, QREAD=1060/QSIZE=10288). Disabling Vela chaining/forwarding did not
resolve it (b67/d68, QREAD=1060/QSIZE=10012). Earlier MRAM and diagnostic SRAM
trials also stalled. Thus the evidence does not establish an OSPI-only,
int16-only or depthwise-only cause.

Known-working KWS completed its int8 depthwise tests. A similarly sized
handwriting model completed direct-OSPI inference with matching routing and
100 MHz flash. These are platform controls, not shape-matched YAMNet controls.
No full-model SRAM copy is part of the final application.

## Recovery and next investigation

Use the public pinned sources/provenance in [upstream](upstream/), local
[conversion instructions](CONVERSION.md), [quantize.py](quantize.py), and
[compile_diagnostic.py](compile_diagnostic.py). The original export's
[generation record](ospi_variant/generation.json) and identical command/weight
stream [provenance](ospi_variant/benchmark_vela_provenance.json) are retained.
Rejected PTE binaries are not all archived; their hashes identify what was
actually tested. Rebuilding with different tool versions is a new experiment.

To reopen: create separately named diagnostic artifacts for each isolated row
above, retain exact compiler versions, quantization scales/zero points, logical
and compiled layouts, command/weight streams and hashes. Verify CRC and matching
symbols before execution; record completion count, PMU overflow, STATUS,
QREAD/QSIZE, IRQ/error state and routing through CMSIS tools. Compare with a
shape-matched successful control. Isolate one operator before attributing the
stall to a particular convolution or reporting a compiler/hardware issue.
Do not overwrite the working hybrid artifacts or relax numerical acceptance.

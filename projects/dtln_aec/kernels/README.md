# DTLN CPU operator overrides

These kernels replace selected **ExecuTorch CPU operators inside the `stage1`
and `stage2` model methods**. Both methods keep part of their computation on
the Cortex-M55. The Ethos-U85 executes the Vela-compiled delegates for the
quantized parts of the model. Both methods list all eight overridden
operators in their CPU partitions. [Partition summary](../model/partition_summary.json)

## What they do

| Files | ExecuTorch operators | Purpose |
| --- | --- | --- |
| `normalization.cpp`, `float_loops.hpp` | `aten::mean.out`, `aten::sub.out`, `aten::pow.Tensor_Scalar_out`, `aten::add.out`, `aten::rsqrt.out`, `aten::mul.out` | Run dense float32 arithmetic for the model's instant normalization. It subtracts the mean, computes variance, and applies reciprocal standard deviation and learned scale and bias. The same overrides also handle matching operations elsewhere in the model, such as squaring for stage 1 log-power features. |
| `quantization.cpp`, `quantization_loops.hpp` | `quantized_decomposed::quantize_per_tensor.out` | Convert float32 activations to signed int16 at the CPU boundaries of quantized linear operators. The fast path handles dense, matching, nonoverlapping tensors with zero point zero, finite positive scale, and nearest-even rounding. It uses Helium/MVE when available. |
| `quantization.cpp`, `quantization_loops.hpp` | `quantized_decomposed::dequantize_per_tensor.out` | Convert quantized activations back to float32 for subsequent model operations. The wrapper records timing and uses the pack's native implementation by default. `DTLN_FAST_DEQUANTIZE` enables the guarded local int16-to-float32 path. |

The wrappers check shapes, types, layouts, and other requirements before
taking a fast path. Cases outside those requirements call the original
ExecuTorch pack kernel. CPU operators outside this table also use the pack's
registry. [Operator registration](normalization.cpp) · [Quantization guards](quantization.cpp) · [Model definition](../model/model.py)

## Where the overrides are enabled

The `ReleaseSRAM` firmware builds these sources and defines
`DTLN_USE_FAST_CPU_KERNELS`. During startup, `dtln_runtime_init()` installs the
eight-entry registry on DTLN's `EmbeddedModule` before either method loads.
That registry serves both the startup benchmarks and the streaming graph
through `dtln_forward()`. Direct-OSPI `Debug` and `Release` builds use the
ExecuTorch pack kernels. [Build configuration](../dtln_aec.cproject.yml) · [Runtime](../dtln_runtime.cpp)

The overrides address the **8 ms hop budget**. On the earlier PTE staged in
SRAM, the two model adapter medians summed to **11.69 ms** with the pack kernels
and **1.74 ms** with the guarded overrides. The measurements used the same
PTE and Vela partitioning. The current PTE's recorded ReleaseSRAM USB replay
peaked at **4.505 ms per hop** for the complete graph. [DTLN benchmark](../model/BENCHMARK.md)

## Where signal processing runs

CMSIS Stream nodes handle overlapping audio windows, forward FFTs, spectral
magnitudes, mask application, inverse FFT, and overlap-add. The DTLN model
nodes invoke ExecuTorch within that graph. The local kernels run during those
model calls, while audio PCM conversion runs in the speaker node. [Graph definition](../app.py) · [Model node](../nodes_impl/DtlnMethod.hpp) · [Speaker node](../nodes_impl/SpeakerOutput.hpp)

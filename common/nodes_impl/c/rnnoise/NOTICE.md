# RNNoise DSP provenance

RNNoiseFeatureProcessor.cc/.hpp adapted from Arm ML Embedded Evaluation Kit,
Alif fork commit 739e3a87327f385a170a5c8c8fb9d181a3ae4f45:
https://github.com/alifsemi/alif_ml-embedded-evaluation-kit/tree/739e3a87327f385a170a5c8c8fb9d181a3ae4f45/source/lib/mlek/use_case/noise_reduction

Original RNNoiseFeatureProcessor.cc SHA-256:
ee72d4ff9841498ea79a247a057f538a0af6d746f8b89a5c455eafec37875e9b.

Original Apache-2.0 headers are preserved. Only these two source files were copied.
Adaptations: exact ML-zoo 480/960 and pitch constants, portable radix-2/3/5 FFT
adapter, silence features zeroed like original ML-zoo Python; history-only reset retains
immutable FFT/window/DCT tables across capture gaps. Analysis and
synthesis nodes use independent instances, owning their respective histories.
The unused histories in either instance are bounded overhead. No imported
library or pack was modified. rnnoise_math.hpp is original project code.
Apache license: LICENSE in this directory.

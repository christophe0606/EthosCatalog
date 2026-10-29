# MobileNet reference comparison

YAMNet uses a MobileNet V1-style backbone, according to its
[official README](https://github.com/tensorflow/models/tree/master/research/audioset/yamnet).
The user supplied the [ML-zoo MobileNet V2 reference](https://github.com/Arm-Examples/ML-zoo/tree/master/models/image_classification/mobilenet_v2_1.0_224)
to help investigate the convolution stalls. Its Apache-licensed INT8 TFLite
model was inspected without importing another project or compiling that model
for the board. The temporary download is not a project dependency.

`MOBILENET_V2_REFERENCE.json` records the model URL, published SHA-1
`8de7996dfeadb5ab6f09e3114f3905fd03879eee`, 4,020,936-byte size, actual tensor
types, operator counts, and the first convolution/depthwise parameters.
The source [README](https://github.com/Arm-Examples/ML-zoo/blob/master/models/image_classification/mobilenet_v2_1.0_224/tflite_int8/README.md)
and `definition.yaml` identify this as the pretrained INT8 variant.

| Property | YAMNet | Inspected MobileNet V2 |
| --- | --- | --- |
| Input | 96x64 log-mel patch, one channel | 224x224 RGB, three channels |
| Initial convolution | 3x3, stride 2, 32 outputs, SAME padding | Same kernel/stride/output count and padding |
| Depthwise convolution | 3x3, multiplier 1, strides 1 or 2 | Same kernel/multiplier and stride choices |
| Nonlinearity | ReLU | Fused ReLU6 on inspected spatial convolutions |
| Output | 521 independent sigmoid scores | 1001 ImageNet softmax scores |
| Deployment precision under investigation | Initially int16 activations/int8 weights; see selected conversion report | INT8 activations and weights, INT32 biases |

The flatbuffer contains 36 regular convolutions, 17 depthwise convolutions,
10 residual adds, one average pool, one reshape and one softmax. It has 120
INT8 tensors, 54 INT32 tensors and no INT16 tensors. Inspected convolution
weights use per-output-channel quantization; depthwise weights use axis 3.

This confirms that the kernel sizes and strides are conventional MobileNet
operations. It does not establish that the failing YAMNet int16 command stream
uses the same hardware/compiler path. ReLU6 also bounds activations, whereas
YAMNet's ReLU does not; that difference matters when comparing quantization
ranges. Replacing YAMNet's activation with ReLU6 would change its function and
was not used as a workaround.

The existing KWS INT8 board control and handwriting direct-OSPI control were
actually run; this MobileNet V2 reference was inspected only. See
`HARDWARE_INVESTIGATION.md` for those hardware results and the selected
conversion report for final deployment details.

# Reused material and licenses

`yolo-fastest_192_face_v4.tflite` is extracted, without modification, from
`object-detection/src/yolo-fastest_192_face_v4.tflite.cpp` in the public
[Arm ML Eval Kit CMSIS examples](https://github.com/Arm-Examples/mlek-cmsis-pack-examples/tree/main/object-detection).
Original notice: **Copyright 2023, Arm Limited and affiliates.**
SPDX-License-Identifier: **Apache-2.0**. The source identifies Emza's
[ModelZoo v1.0 object detection](https://github.com/emza-vs/ModelZoo/tree/v1.0/object_detection/)
as the model origin. Its model license is Apache-2.0.

`sample_image.rgb` contains the unmodified bytes of the example's
`object-detection/src/sample_image.cpp` array. Original notice:
**Copyright 2022 Arm Limited and/or its affiliates
<open-source-office@arm.com>**. SPDX-License-Identifier: **Apache-2.0**.
The upstream generated-file comment identifies `setup.png` as the image source.

`decode.py` translates the one-class box-decoding and NMS formulas from
`DetectorPostProcessing.cc`, `DetectorPostProcessing.hpp` and `ImageUtils.cc`
in Arm ML Eval Kit UC API 22.8.0-Beta. `patterns.py` translates the grayscale
formula and signed-byte conversion from `ImageUtils.cc`. Original notice:
**Copyright (c) 2022 Arm Limited. All rights reserved.**
SPDX-License-Identifier: **Apache-2.0**. Public project:
https://gitlab.arm.com/artificial-intelligence/ethos-u/ml-embedded-evaluation-kit

The original Apache License, Version 2.0 is preserved in
`LICENSE.Apache-2.0.txt`. Original source attribution is preserved in this
notice; derivative files carry Apache-2.0 SPDX identifiers. The project-owned
C grayscale and decoder/NMS implementations preserve the same source formulas
and contracts described above. Only the two small binary assets were extracted
from the examples;
the full upstream project and its TFLM dependency tree were not copied.

The portable grayscale implementation is now
`common/nodes_impl/c/rgb888_to_gray_int8.c`, and single-class IoU/NMS is
`common/nodes_impl/c/non_maximum_suppression.c`. Both retain the original
2022 Arm notice and Apache-2.0 identifier, with 2026 modification notices.
Their shared Stream wrappers and image/box view types impose no detector model
or rendering policy. RGB565 center crop/bit expansion is new Apache-2.0 code.

Conversion uses the repository's existing PyTorch, TorchAO, ExecuTorch, Vela,
NumPy, FlatBuffers and safetensors host dependencies. The isolated comparison
environment uses TensorFlow 2.20.0 (Apache-2.0); neither TensorFlow nor its Python
dependencies are linked into firmware. No GPL code or dependency was added to
this project.

## CMSIS Stream configuration snapshots

The four `RTE/CMSIS` application/runtime configuration `.base@1.0.0`
snapshots (`app_config.hpp`, `stream_init.cpp`, `stream_init.hpp` and
`stream_runtime_config.hpp`) are unchanged copies of the corresponding files
in `thirdparty/CMSIS-Stream/platform/cmsis_rtos_runtime/config/`. Their origin
is the repository's existing CMSIS Stream dependency, distributed under its
[Apache-2.0 license](../../../thirdparty/CMSIS-Stream/LICENSE). The snapshots
retain their supplied text; no new ownership notice has been invented for them.
The active project forwarding adapters carry their own Arm 2026/Apache-2.0
headers. Other CMSIS Stream and RTX base files retain their supplied notices.

## Board platform code

The detector uses the existing Alif DevKit-E8 board adapter in
`common/nodes_impl/alif_e8/lcd.c`. This file retains its original Alif
Semiconductor notice and is governed by the
[Alif Semiconductor Software License Agreement](https://alifsemi.com/license/).
The agreement permits source and binary redistribution subject to its
conditions, including use only on Alif Semiconductor products and preservation
of notices and terms. Alif board-support-pack components retain their own pack
terms. The new Arm-authored camera adapter carries Apache-2.0. This platform
license note does not change the Apache-2.0 licensing of the detector's
project-owned processing and model conversion code.

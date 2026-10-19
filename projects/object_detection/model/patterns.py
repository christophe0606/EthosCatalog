# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Small deterministic conversion suite; synthetic images are not calibration."""
from pathlib import Path
import numpy as np

SEED = 20261019


def preprocess(rgb):
    # MLEK ImageUtils.cc: float32 BT.601 weights, truncate uint8, subtract 128.
    rgb = np.asarray(rgb, dtype=np.float32)
    gray = (rgb[..., 0] * np.float32(.299) + rgb[..., 1] * np.float32(.587) + rgb[..., 2] * np.float32(.114)).astype(np.uint8)
    return (gray.astype(np.int16) - 128).astype(np.int8)[None, ..., None]


def patterns():
    rgb = np.frombuffer(Path(__file__).with_name("sample_image.rgb").read_bytes(), dtype=np.uint8).reshape(192, 192, 3)
    names, values = ["source_sample"], [preprocess(rgb)]
    for name, image in (("sample_mirror", rgb[:, ::-1]), ("sample_dark", rgb // 2),
                        ("sample_bright", np.minimum(rgb.astype(np.int16) + 30, 255)),
                        ("sample_shift", np.roll(rgb, 8, axis=1))):
        names.append(name)
        values.append(preprocess(image))
    for value in (-128, -64, 0, 64, 127):
        names.append(f"constant_{value}")
        values.append(np.full((1, 192, 192, 1), value, dtype=np.int8))
    y, x = np.mgrid[:192, :192]
    for name, image in (("horizontal_ramp", x * 255 // 191 - 128),
                        ("vertical_ramp", y * 255 // 191 - 128),
                        ("checker", ((x + y) % 2) * 255 - 128),
                        ("blocks", ((x//16 + y//16) % 2) * 255 - 128)):
        names.append(name)
        values.append(image.astype(np.int8)[None, ..., None])
    for y, x in ((0, 0), (0, 191), (191, 0), (191, 191), (96, 96)):
        value = np.full((1, 192, 192, 1), -128, dtype=np.int8)
        value[0, y, x, 0] = 127
        names.append(f"impulse_{y}_{x}")
        values.append(value)
    rng = np.random.default_rng(SEED)
    for i in range(24):
        names.append(f"random_{i}")
        values.append(rng.integers(-128, 128, (1, 192, 192, 1), dtype=np.int8))
    return names, np.stack(values)

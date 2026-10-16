# SPDX-License-Identifier: Apache-2.0
"""Deterministic synthetic MFCC-shaped tensors; no downloaded test patterns."""
import numpy as np

SEED = 20261016


def fake_inputs(random_count=256):
    rng = np.random.default_rng(SEED)
    shape = (1, 49, 10, 1)
    inputs, names = [], []

    def add(name, values):
        names.append(name)
        inputs.append(np.broadcast_to(values, shape).astype(np.int8).copy())

    for value in (-128, -64, -5, 0, 64, 127):
        add(f"constant_{value}", value)
    add("ramp", np.linspace(-128, 127, 490).reshape(shape))
    add("checkerboard", np.where(np.indices(shape).sum(axis=0) % 2, 127, -128))
    for h, w in ((0, 0), (24, 5), (48, 9)):
        x = np.full(shape, -5, dtype=np.int8)
        x[0, h, w, 0] = 127
        add(f"impulse_{h}_{w}", x)
    for amplitude in (8, 32, 127):
        time = np.arange(49).reshape(1, 49, 1, 1)
        coeff = np.arange(10).reshape(1, 1, 10, 1)
        add(f"sinusoid_{amplitude}", np.clip(-5 + amplitude*np.sin(time*.31+coeff*.7), -128, 127))
    for i in range(random_count):
        if i % 2:
            x = np.clip(np.rint(rng.normal(-5, (8, 24, 48)[i % 3], shape)), -128, 127)
        else:
            x = rng.integers(-128, 128, shape)
        add(f"random_{i}", x)
    return names, np.stack(inputs)

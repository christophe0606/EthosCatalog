#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run only the original TFLite interpreter, in a separate TensorFlow env."""
import argparse
from pathlib import Path
import numpy as np
import tensorflow as tf


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--outputs", type=Path, required=True)
    args = parser.parse_args()
    inputs = np.load(args.inputs, allow_pickle=False)
    # Explicit reference kernels keep comparisons independent of XNNPACK and
    # other platform-specific delegates. Also check the default CPU kernels.
    outputs = {}
    for mode, kwargs in (
        ("reference", {"experimental_op_resolver_type": tf.lite.experimental.OpResolverType.BUILTIN_REF}),
        ("default", {}),
    ):
        interpreter = tf.lite.Interpreter(model_path=str(args.source), num_threads=1, **kwargs)
        interpreter.allocate_tensors()
        inp, out = interpreter.get_input_details()[0], interpreter.get_output_details()[0]
        assert inp["dtype"] == np.int8 and tuple(inp["shape"]) == (1, 49, 10, 1)
        assert out["dtype"] == np.int8 and tuple(out["shape"]) == (1, 12)
        values = []
        for x in inputs:
            interpreter.set_tensor(inp["index"], x)
            interpreter.invoke()
            values.append(interpreter.get_tensor(out["index"]))
        outputs[mode] = np.stack(values)
    np.savez(args.outputs, tensorflow_version=np.array(tf.__version__), **outputs)


if __name__ == "__main__":
    main()

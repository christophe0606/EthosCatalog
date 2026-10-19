#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Run original pre-Vela model in an isolated TensorFlow CPU interpreter."""
import argparse
from pathlib import Path
import numpy as np
import tensorflow as tf


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "inputs", "outputs"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    inputs = np.load(args.inputs, allow_pickle=False)
    outputs = {}
    for mode, kwargs in (("reference", {"experimental_op_resolver_type": tf.lite.experimental.OpResolverType.BUILTIN_REF}),
                         ("default", {})):
        interpreter = tf.lite.Interpreter(model_path=str(args.source), num_threads=1, **kwargs)
        interpreter.allocate_tensors()
        inp, out = interpreter.get_input_details()[0], interpreter.get_output_details()
        assert inp["dtype"] == np.int8 and tuple(inp["shape"]) == (1, 192, 192, 1)
        values = [[], []]
        for x in inputs:
            interpreter.set_tensor(inp["index"], x)
            interpreter.invoke()
            for i, tensor in enumerate(out):
                values[i].append(interpreter.get_tensor(tensor["index"]))
        for i, values in enumerate(values):
            outputs[f"{mode}_{i}"] = np.stack(values)
    np.savez(args.outputs, tensorflow_version=np.array(tf.__version__), **outputs)


if __name__ == "__main__":
    main()

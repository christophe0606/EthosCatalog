#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Verify op100's explicit Q/DQ clamp and the exact 2x nearest mapping."""
import json
from pathlib import Path
import numpy as np
import torch
from model import YoloFastest, q, dq


def main():
    model = YoloFastest()
    params = model.layers[100].out
    scale, zero = params
    boundaries = (np.arange(-129, 129, dtype=np.float32) + np.float32(.5-zero)) * np.float32(scale)
    values = np.concatenate([boundaries, np.nextafter(boundaries, np.float32(-np.inf)),
                             np.nextafter(boundaries, np.float32(np.inf)),
                             np.linspace(-100, 100, 200001, dtype=np.float32),
                             np.array([-1e6, 0, 6, 1e6], dtype=np.float32)])
    tensor = torch.from_numpy(values)
    expected = q(torch.clamp(tensor, 0, 6), params)
    actual = q(torch.clamp(dq(q(tensor, params), params), 0, 6), params)
    torch.testing.assert_close(actual, expected, rtol=0, atol=0)
    # Source resize has half_pixel_centers=True, align_corners=False, 6 ->12.
    output = np.arange(12, dtype=np.float32)
    source_map = np.floor((output+.5)/2).astype(int)
    aten_map = np.floor(output/2).astype(int)
    np.testing.assert_array_equal(source_map, aten_map)
    report = {"conv_source_op_index": 100, "output_scale": scale, "output_zero_point": zero,
              "quantized_clamp_interval": [int(q(torch.tensor(0.), params)), int(q(torch.tensor(6.), params))],
              "scalar_boundary_checks": len(values), "clamp_bit_exact": True,
              "resize_input_hw": [6,6], "resize_output_hw": [12,12],
              "source_half_pixel_centers": True, "source_align_corners": False,
              "nearest_coordinate_map": source_map.tolist(), "nearest_bit_exact": True}
    Path(__file__).with_name("representation_validation.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()

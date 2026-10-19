# Copyright (c) 2022 Arm Limited. All rights reserved.
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""NumPy translation of MLEK DetectorPostProcessing.cc's one-class decode/NMS."""
import numpy as np

ANCHORS = (((38, 77), (47, 97), (61, 126)), ((14, 26), (19, 37), (28, 55)))
QPARAMS = ((0.13408391177654266, 47), (0.18535925447940826, 10))


def iou(a, b):
    a, b = np.asarray(a), np.asarray(b)
    inter = np.maximum(np.minimum(a[2:], b[2:]) - np.maximum(a[:2], b[:2]), 0).prod()
    union = (a[2:]-a[:2]).prod() + (b[2:]-b[:2]).prod() - inter
    return float(inter/union) if union > 0 else 0.


def decode(heads, threshold=.5, nms=.45):
    candidates = []
    for index, (head, anchors, (scale, zero)) in enumerate(zip(heads, ANCHORS, QPARAMS)):
        size = head.shape[1]
        values = (head.astype(np.float32).reshape(size, size, 3, 6) - zero) * scale
        for row in range(size):
            for col in range(size):
                for anchor in range(3):
                    x, y, w, h, obj, cls = values[row, col, anchor]
                    objectness = float(1/(1+np.exp(-obj)))
                    score = objectness * float(1/(1+np.exp(-cls)))
                    if objectness <= threshold or score <= threshold:
                        continue
                    cx = (float(1/(1+np.exp(-x))) + col) * 192/size
                    cy = (float(1/(1+np.exp(-y))) + row) * 192/size
                    width, height = np.exp(w) * anchors[anchor][0], np.exp(h) * anchors[anchor][1]
                    # NMS must use unclipped floating boxes, as in MLEK.
                    box = [float(cx-width/2), float(cy-height/2), float(cx+width/2), float(cy+height/2)]
                    candidates.append({"score": score, "box": box, "head": index, "cell": [row, col, anchor]})
    candidates.sort(key=lambda value: value["score"], reverse=True)
    result = []
    while candidates:
        best = candidates.pop(0)
        candidates = [value for value in candidates if iou(best["box"], value["box"]) <= nms]
        result.append(best)
    for value in result:
        value["box"] = np.clip(value["box"], 0, 192).tolist()
    return result

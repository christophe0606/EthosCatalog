# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
import math
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count

class NonMaximumSuppression(ProjectNode, GenericNode):
    """Stable score ordering and single-class IoU NMS; leaves boxes unclipped."""
    def __init__(self, name, max_boxes, iou_threshold):
        GenericNode.__init__(self, name, identified=False)
        if not math.isfinite(iou_threshold) or not 0 <= iou_threshold <= 1:
            raise ValueError("IoU threshold must be finite and in [0,1]")
        self.addInput("i", CStructType("ScoredBoxList", 16), 1)
        self.addOutput("o", CStructType("ScoredBoxList", 16), 1)
        self.params = dict(max_boxes=sample_count(max_boxes), iou_threshold=float(iou_threshold))
        if max_boxes > (2**31-1)//20:
            raise ValueError("Box capacity is too large")
        self.parameters()

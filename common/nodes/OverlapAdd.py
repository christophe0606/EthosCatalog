# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count


class OverlapAdd(ProjectNode, GenericNode):
    """Add overlapping frames and emit one hop without window normalization."""
    def __init__(self, name, frame_samples, hop, *, identified=False):
        GenericNode.__init__(self, name, identified=identified)
        frame_samples, hop = sample_count(frame_samples), sample_count(hop)
        if hop > frame_samples:
            raise ValueError("Hop cannot exceed the frame length")
        self.addInput("i", CType(F32), frame_samples)
        self.addOutput("o", CType(F32), hop)

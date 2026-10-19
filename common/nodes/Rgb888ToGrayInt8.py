# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count

class Rgb888ToGrayInt8(ProjectNode, GenericNode):
    """Truncated float32 BT.601 RGB luma, then byte offset -128; owns output."""
    def __init__(self, name, max_pixels):
        GenericNode.__init__(self, name, identified=False)
        self.addInput("i", CStructType("Rgb888Image", 24), 1)
        self.addOutput("o", CStructType("GrayInt8Image", 24), 1)
        self.params = dict(max_pixels=sample_count(max_pixels))
        if max_pixels > 2**31-1:
            raise ValueError("Gray capacity is too large")
        self.parameters()

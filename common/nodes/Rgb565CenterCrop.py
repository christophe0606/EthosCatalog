# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count

class Rgb565CenterCrop(ProjectNode, GenericNode):
    """RGB565 image view -> node-owned, tightly packed RGB888 center crop."""
    def __init__(self, name, width, height):
        GenericNode.__init__(self, name, identified=False)
        self.addInput("i", CStructType("Rgb565Image", 24), 1)
        self.addOutput("o", CStructType("Rgb888Image", 24), 1)
        self.params = dict(width=sample_count(width), height=sample_count(height))
        if width * height > (2**31-1)//3:
            raise ValueError("RGB crop is too large")
        self.parameters()

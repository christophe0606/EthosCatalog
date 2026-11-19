# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode


class PreservingFFT(ProjectNode, GenericNode):
    """512-point RFFT with a private input copy for shared upstream buffers."""
    def __init__(self, name):
        GenericNode.__init__(self, name, identified=False)
        self.addInput("i", CType(F32), 512)
        self.addOutput("o", CType(F32), 512)

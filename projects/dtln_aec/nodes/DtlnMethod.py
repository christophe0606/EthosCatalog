# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode


class DtlnMethod(ProjectNode, GenericNode):
    """One PTE method with its own retained recurrent state."""
    folder = "../nodes_impl"
    needsHardware = True

    def __init__(self, name, stage):
        GenericNode.__init__(self, name, identified=False)
        width = 257 if stage == 1 else 512
        self.addInput("i1", CType(F32), width)
        self.addInput("i2", CType(F32), width)
        self.addOutput("o", CType(F32), width)
        self.params = {"stage": stage}
        self.parameters()

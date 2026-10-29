# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode
class SileroInference(ProjectNode, GenericNode):
    folder='../nodes_impl'
    needsHardware=True
    def __init__(self,name):
        GenericNode.__init__(self,name,identified=False)
        self.addInput('i',CType(F32),512)
        self.addOutput('o',CType(F32),1)
        self.params={}
        self.parameters()

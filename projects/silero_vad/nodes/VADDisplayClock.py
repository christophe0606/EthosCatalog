# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode
class VADDisplayClock(ProjectNode, GenericSink):
    folder='../nodes_impl'
    needsHardware=True
    def __init__(self,name):
        GenericSink.__init__(self,name,identified=False, selectors=['vad_snapshot'])
        self.addInput('i',CType(F32),1)
        self.addEventOutput()
        self.params=dict(fps=20,threshold=0.5)
        self.parameters()

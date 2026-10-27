# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode
class WaveformSink(ProjectNode, GenericSink):
    folder='../nodes_impl'
    needsHardware=True
    def __init__(self,name):
        GenericSink.__init__(self,name,identified=False,selectors=['wave'])
        self.addInput('i',CStructType('AudioBlock',332),1)
        self.addEventOutput()
        self.parameters()

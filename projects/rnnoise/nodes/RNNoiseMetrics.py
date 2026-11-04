# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode
class RNNoiseMetrics(ProjectNode,GenericSink):
    folder='../nodes_impl'
    needsHardware=True
    def __init__(self,name):
        GenericSink.__init__(self,name,identified=False,selectors=['rn_snapshot'])
        self.addInput('i',CType(F32),483)
        self.addEventOutput()
        self.params={}
        self.parameters()

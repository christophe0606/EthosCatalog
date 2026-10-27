# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode
class PlaybackSource(ProjectNode, GenericNode):
    folder='../nodes_impl'
    needsHardware=True
    def __init__(self,name):
        GenericNode.__init__(self,name,identified=False,selectors=['advance','ack'])
        self.addInput('i',CType(UINT32),1)
        self.addOutput('o',CStructType('AudioBlock',332),1)
        self.addEventInput(); self.addEventOutput()
        self.parameters()

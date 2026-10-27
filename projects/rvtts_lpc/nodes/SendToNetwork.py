# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode
class SendToNetwork(ProjectNode, GenericNode):
    folder='../nodes_impl'
    needsHardware=True
    def __init__(self,name):
        GenericNode.__init__(self,name,identified=False,selectors=['request','ack'])
        self.addInput('i',CStructType('TextLine',168),1)
        self.addOutput('o',CType(UINT32),1)
        self.addEventInput(); self.addEventOutput()
        self.parameters()

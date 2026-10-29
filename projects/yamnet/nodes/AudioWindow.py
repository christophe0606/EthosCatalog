# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode
class AudioWindow(ProjectNode,GenericNode):
    folder='../nodes_impl'
    needsHardware=False
    def __init__(self,name):
        GenericNode.__init__(self,name,identified=True,selectors=['request','complete','result'])
        self.addInput('i',CType(F32),160)
        self.addOutput('o',CType(F32),400)

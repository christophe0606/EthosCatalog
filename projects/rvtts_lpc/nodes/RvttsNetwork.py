# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode
class RvttsNetwork(ProjectNode, BaseNode):
    folder='../nodes_impl'
    needsHardware=True
    def __init__(self,name, method):
        BaseNode.__init__(self,name,identified=False,selectors=['advance'])
        self.addEventInput(); self.addEventOutput()
        self.params=dict(method=method)
        self.parameters()

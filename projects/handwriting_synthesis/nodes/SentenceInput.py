# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode
class SentenceInput(ProjectNode, GenericNode):
    folder = '../nodes_impl'
    needsHardware = True
    def __init__(self, name):
        GenericNode.__init__(self, name, identified=False)
        self.addInput('i', CStructType('UartBlock', 36), 1)
        self.addOutput('o', CStructType('SentenceSnapshot', 84), 1)
        self.parameters()

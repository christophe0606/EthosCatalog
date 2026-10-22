# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode
class UartSource(ProjectNode, GenericSource):
    folder = '../nodes_impl'
    needsHardware = True
    def __init__(self, name):
        GenericSource.__init__(self, name, identified=False)
        self.addOutput('o', CStructType('UartBlock', 36), 1)
        self.parameters()

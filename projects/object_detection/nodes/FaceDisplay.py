# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode

class FaceDisplay(ProjectNode, GenericSink):
    folder = "../nodes_impl"
    needsHardware = True
    def __init__(self, name):
        GenericSink.__init__(self, name, identified=False)
        self.addInput("boxes", CStructType("ScoredBoxList", 16), 1)
        self.addInput("frame", CStructType("PreparedFrame", 16), 1)
        self.params = dict(period_ms=200)
        self.parameters()

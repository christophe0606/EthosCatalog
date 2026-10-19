# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode

class CameraCapture(ProjectNode, GenericSource):
    folder = "../nodes_impl"
    needsHardware = True
    def __init__(self, name):
        GenericSource.__init__(self, name, identified=False)
        self.addOutput("o", CStructType("Rgb565Image", 24), 1)
        self.parameters()

# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import CType, F32, GenericSink
from common.nodes._base import ProjectNode


class SpeakerOutput(ProjectNode, GenericSink):
    folder = "../nodes_impl"
    needsHardware = True

    def __init__(self, name, samples):
        super().__init__(name, identified=False)
        self.addInput("i", CType(F32), samples)
        self.parameters()

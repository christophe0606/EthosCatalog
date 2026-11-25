# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import BaseNode
from common.nodes._base import ProjectNode


class WaveformDisplay(ProjectNode, BaseNode):
    folder = "../nodes_impl"
    needsHardware = True

    def __init__(self, name):
        super().__init__(name, identified=False, selectors=["waveform"])
        self.addEventInput()
        self.parameters()

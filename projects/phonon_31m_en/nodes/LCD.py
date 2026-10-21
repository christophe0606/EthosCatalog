# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode

class LCD(ProjectNode, BaseNode):
    """This application's spectrum display, animation and touch interaction."""
    folder = "../nodes_impl"
    needsHardware = True
    def __init__(self, name, fft_size, sample_rate, fps=20):
        BaseNode.__init__(self, name, identified=True, selectors=["spectrum"])
        self.addEventInput()
        self.params = dict(fft_size=int(fft_size), sample_rate=int(sample_rate), fps=int(fps))
        self.parameters()

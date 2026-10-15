# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode

class LCD(ProjectNode, BaseNode):
    needsHardware = True
    def __init__(self, name, fft_size, sample_rate, fps=20):
        BaseNode.__init__(self, name, identified=True, selectors=["spectrum"])
        self.addEventInput()
        self.params = dict(fft_size=int(fft_size), sample_rate=int(sample_rate), fps=int(fps))
        self.parameters()

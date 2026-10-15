# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode

class SendToLCD(ProjectNode, GenericSink):
    needsHardware = True
    def __init__(self, name, spectrum_type, sample_rate, block_samples, fps=20):
        GenericSink.__init__(self, name, identified=False, selectors=["spectrum"])
        self.addInput("i", spectrum_type, 1)
        self.addEventOutput()
        self.params = dict(sample_rate=int(sample_rate), block_samples=int(block_samples), fps=int(fps))
        self.parameters()

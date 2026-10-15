# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode

class Magnitude(ProjectNode, GenericNode):
    def __init__(self, name, fft_type, spectrum_type):
        GenericNode.__init__(self, name, identified=False)
        self.addInput("i", fft_type, 1)
        self.addOutput("o", spectrum_type, 1)

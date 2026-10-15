# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode

class EmbeddedGain(ProjectNode, GenericNode):
    needsHardware = True
    def __init__(self, name, audio_type):
        GenericNode.__init__(self, name, identified=False)
        self.addInput("i", audio_type, 1)
        self.addOutput("o", audio_type, 1)
        self.parameters()

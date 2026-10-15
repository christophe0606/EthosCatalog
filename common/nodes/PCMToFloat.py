# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode

class PCMToFloat(ProjectNode, GenericNode):
    def __init__(self, name, pcm_type, audio_type):
        GenericNode.__init__(self, name, identified=False)
        self.addInput("i", pcm_type, 1)
        self.addOutput("o", audio_type, 1)

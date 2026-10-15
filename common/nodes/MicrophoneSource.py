# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode

class MicrophoneSource(ProjectNode, GenericSource):
    needsHardware = True
    def __init__(self, name, pcm_type):
        GenericSource.__init__(self, name, identified=False)
        self.addOutput("o", pcm_type, 1)
        self.parameters()

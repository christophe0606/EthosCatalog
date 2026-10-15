# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count

class MicrophoneSource(ProjectNode, GenericSource):
    """Produce n mono SINT16 PCM samples, reblocking the hardware capture buffers."""
    needsHardware = True
    def __init__(self, name, samples):
        GenericSource.__init__(self, name, identified=False)
        self.addOutput("o", CType(SINT16), sample_count(samples))
        self.parameters()

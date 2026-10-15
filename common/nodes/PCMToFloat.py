# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count

class PCMToFloat(ProjectNode, GenericNode):
    """Consume n signed SINT16 PCM samples; produce n F32 audio samples in [-1,1)."""
    def __init__(self, name, samples):
        GenericNode.__init__(self, name, identified=False)
        samples = sample_count(samples)
        self.addInput("i", CType(SINT16), samples)
        self.addOutput("o", CType(F32), samples)

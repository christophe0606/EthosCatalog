# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count

class HannWindow(ProjectNode, GenericNode):
    """Consume/produce n F32 audio samples using a periodic Hann window."""
    def __init__(self, name, samples):
        GenericNode.__init__(self, name, identified=False)
        samples = sample_count(samples)
        self.addInput("i", CType(F32), samples)
        self.addOutput("o", CType(F32), samples)

# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count

class RemoveDC(ProjectNode, GenericNode):
    """Consume/produce n F32 time-domain audio samples; remove their mean."""
    def __init__(self, name, samples):
        GenericNode.__init__(self, name, identified=False)
        samples = sample_count(samples)
        self.addInput("i", CType(F32), samples)
        self.addOutput("o", CType(F32), samples)

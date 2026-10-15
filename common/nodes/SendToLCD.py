# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count

class SendToLCD(ProjectNode, GenericSink):
    """Consume bins F32 magnitudes; emit a spectrum event.

    block_samples is the audio hop represented by a spectrum, not the bin count.
    """
    needsHardware = True
    def __init__(self, name, bins, sample_rate, block_samples, fps=20):
        GenericSink.__init__(self, name, identified=False, selectors=["spectrum"])
        self.addInput("i", CType(F32), sample_count(bins))
        self.addEventOutput()
        self.params = dict(sample_rate=int(sample_rate), block_samples=sample_count(block_samples), fps=int(fps))
        self.parameters()

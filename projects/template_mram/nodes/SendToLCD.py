# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode, sample_count

class SendToLCD(ProjectNode, GenericSink):
    """Publish this application's F32 spectrum events and track audio timing.

    block_samples is the audio hop represented by a spectrum, not the bin count.
    """
    folder = "../nodes_impl"
    needsHardware = True
    def __init__(self, name, bins, sample_rate, block_samples, fps=20):
        GenericSink.__init__(self, name, identified=False, selectors=["spectrum"])
        self.addInput("i", CType(F32), sample_count(bins))
        self.addEventOutput()
        self.params = dict(sample_rate=int(sample_rate), block_samples=sample_count(block_samples), fps=int(fps))
        self.parameters()

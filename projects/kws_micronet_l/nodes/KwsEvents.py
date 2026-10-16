# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode, sample_count
class KwsEvents(ProjectNode, GenericSink):
    folder='../nodes_impl'
    needsHardware=True
    def __init__(self,name,samples):
        GenericSink.__init__(self,name,identified=False,selectors=['display_tick'])
        self.addInput('i',CType(F32),sample_count(samples))
        self.addEventOutput()
        self.params=dict(fps=20)
        self.parameters()

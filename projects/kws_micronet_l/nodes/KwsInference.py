# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode, sample_count
class KwsInference(ProjectNode, GenericSink):
    folder='../nodes_impl'
    needsHardware=True
    def __init__(self,name,samples):
        GenericSink.__init__(self,name,identified=False,selectors=['keyword'])
        self.addInput('i',CType(F32),sample_count(samples))
        self.addEventOutput()
        self.params=dict(threshold=0.7)
        self.parameters()

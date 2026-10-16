# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode
class KeywordLCD(ProjectNode, BaseNode):
    folder='../nodes_impl'
    needsHardware=True
    def __init__(self,name):
        BaseNode.__init__(self,name,identified=True,selectors=['keyword','display_tick'])
        self.addEventInput()
        self.params=dict(fade_ms=1000,fps=20)
        self.parameters()

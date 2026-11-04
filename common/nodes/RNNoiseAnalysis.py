# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode
class RNNoiseAnalysis(ProjectNode,GenericNode):
    """Exact48kHz480-sample RNNoise analysis; features and spectral context."""
    needsHardware=True
    def __init__(self,name):
        GenericNode.__init__(self,name,identified=False)
        self.addInput('i',CType(SINT16),480)
        self.addOutput('o1',CType(F32),42)
        self.addOutput('o2',CType(F32),1993)
        self.parameters()

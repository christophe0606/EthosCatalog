# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode
class RNNoiseSynthesis(ProjectNode,GenericNode):
    """RNNoise pitch/mask/OLA synthesis:480 normalized PCM plus input RMS,VAD,mean band gain."""
    needsHardware=True
    def __init__(self,name):
        GenericNode.__init__(self,name,identified=False)
        self.addInput('i1',CType(F32),1993)
        self.addInput('i2',CType(F32),23)
        self.addOutput('o',CType(F32),483)
        self.parameters()

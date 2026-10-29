# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count
class LogMel(ProjectNode, GenericNode):
    """Windowed F32 frame to log mel magnitudes, with injected dense tables."""
    def __init__(self,name,frame_samples,bands,table_symbol):
        GenericNode.__init__(self,name,identified=False)
        self.addInput('i',CType(F32),sample_count(frame_samples))
        self.addOutput('o',CType(F32),sample_count(bands))
        self.addVariableArg(table_symbol)

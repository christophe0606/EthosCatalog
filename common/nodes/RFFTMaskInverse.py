# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count


class RFFTMaskInverse(ProjectNode, GenericNode):
    """Multiply packed RFFT by real bin gains, then normalized inverse RFFT."""
    def __init__(self, name, fft_size):
        GenericNode.__init__(self, name, identified=False)
        fft_size = sample_count(fft_size)
        if fft_size != 512:
            raise ValueError("This implementation supports a 512-point RFFT")
        self.addInput("i1", CType(F32), fft_size)
        self.addInput("i2", CType(F32), fft_size // 2 + 1)
        self.addOutput("o", CType(F32), fft_size)

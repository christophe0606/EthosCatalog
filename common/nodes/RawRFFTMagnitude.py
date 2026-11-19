# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count


class RawRFFTMagnitude(ProjectNode, GenericNode):
    """Unscaled magnitudes of a CMSIS-DSP packed RFFT, including both edges."""
    def __init__(self, name, fft_size):
        GenericNode.__init__(self, name, identified=False)
        fft_size = sample_count(fft_size)
        if fft_size < 4 or fft_size % 2:
            raise ValueError("RFFT length must be even and at least four")
        self.addInput("i", CType(F32), fft_size)
        self.addOutput("o", CType(F32), fft_size // 2 + 1)

# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count

class FFT(ProjectNode, GenericNode):
    """Consume n F32 audio samples; produce n CMSIS-DSP packed RFFT scalars.

    Packed output: Re(X[0]), Re(X[n/2]), Re(X[1]), Im(X[1]), ... .
    DC and Nyquist are real; packing them together fits n/2+1 bins in n floats.
    n is a power of two from 32 to 4096, as supported by CMSIS-DSP."""
    def __init__(self, name, samples):
        GenericNode.__init__(self, name, identified=False)
        samples = sample_count(samples)
        if samples < 32 or samples > 4096 or samples & (samples-1):
            raise ValueError("FFT length must be a power of two from 32 to 4096")
        self.addInput("i", CType(F32), samples)
        self.addOutput("o", CType(F32), samples)

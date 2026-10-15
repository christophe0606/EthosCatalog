# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count

class Magnitude(ProjectNode, GenericNode):
    """Consume fft_size F32 packed RFFT scalars; produce fft_size/2+1 F32 bins.

    Bins include DC and Nyquist. amplitude_scale calibrates interior magnitudes;
    DC/Nyquist use half that scale. A periodic Hann window needs 4/fft_size.
    """
    def __init__(self, name, fft_size, amplitude_scale=1.0):
        GenericNode.__init__(self, name, identified=False)
        fft_size = sample_count(fft_size)
        if fft_size < 4 or fft_size % 2:
            raise ValueError("Magnitude requires an even FFT length of at least 4")
        self.addInput("i", CType(F32), fft_size)
        self.addOutput("o", CType(F32), fft_size//2+1)
        self.params = dict(amplitude_scale=float(amplitude_scale))
        self.parameters()

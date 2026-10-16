# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count

class MFCC(ProjectNode, GenericNode):
    """F32 time frame -> F32 cepstral coefficients, using injected tables.

    table_symbol names a const mfcc_config_t declared by the application.
    The node owns its FFT scratch; tables and log floor belong to the caller.
    """
    def __init__(self, name, frame_samples, features, table_symbol):
        GenericNode.__init__(self, name, identified=False)
        self.addInput('i', CType(F32), sample_count(frame_samples))
        self.addOutput('o', CType(F32), sample_count(features))
        self.addVariableArg(table_symbol)

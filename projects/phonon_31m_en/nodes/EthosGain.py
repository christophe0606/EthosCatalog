# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode, sample_count

class EthosGain(ProjectNode, GenericNode):
    """Consume/produce n F32 audio samples using the Ethos-backed model.

    Input is normalized PCM audio; output uses the model's gain. n must match
    the deployed model's tensor length (512 for phonon_31m_en).
    """
    folder = "../nodes_impl"
    needsHardware = True
    def __init__(self, name, samples):
        GenericNode.__init__(self, name, identified=False)
        samples = sample_count(samples)
        self.addInput("i", CType(F32), samples)
        self.addOutput("o", CType(F32), samples)
        self.parameters()

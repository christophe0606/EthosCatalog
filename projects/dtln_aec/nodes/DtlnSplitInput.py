# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import CType, F32, GenericNode
from common.nodes._base import ProjectNode, sample_count


class DtlnSplitInput(ProjectNode, GenericNode):
    """Split interleaved microphone and loopback samples from one SDS hop."""

    folder = "../nodes_impl"

    def __init__(self, name, samples):
        super().__init__(name, identified=False)
        samples = sample_count(samples)
        self.addInput("i", CType(F32), 2 * samples)
        self.addOutput("o1", CType(F32), samples)
        self.addOutput("o2", CType(F32), samples)

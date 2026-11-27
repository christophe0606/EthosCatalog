# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import CType, F32, SINT32, GenericNode
from common.nodes._base import ProjectNode, sample_count


class EnCodecDecoder(ProjectNode, GenericNode):
    folder = "../nodes_impl"

    def __init__(self, name, codes, samples):
        super().__init__(name)
        self.addInput("i", CType(SINT32), sample_count(codes))
        self.addOutput("o", CType(F32), sample_count(samples))

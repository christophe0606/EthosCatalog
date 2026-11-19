# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import CType, F32, GenericSource
from common.nodes._base import ProjectNode


class DtlnFakeSource(ProjectNode, GenericSource):
    """Synchronized synthetic mic mixture and far-end reference."""

    folder = "../nodes_impl"
    needsHardware = True

    def __init__(self, name, hop_samples):
        GenericSource.__init__(self, name, identified=False)
        self.addOutput("o1", CType(F32), hop_samples)
        self.addOutput("o2", CType(F32), hop_samples)
        self.parameters()

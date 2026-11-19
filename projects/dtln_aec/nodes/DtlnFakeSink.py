# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import CType, F32, GenericSink
from common.nodes._base import ProjectNode


class DtlnFakeSink(ProjectNode, GenericSink):
    """Consume processed audio and summarize the synthetic cases."""

    folder = "../nodes_impl"
    needsHardware = True

    def __init__(self, name, hop_samples):
        GenericSink.__init__(self, name, identified=False)
        self.addInput("i", CType(F32), hop_samples)
        self.parameters()

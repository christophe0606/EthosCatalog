# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import CType, F32, GenericSink
from common.nodes._base import ProjectNode, sample_count


class WaveformCapture(ProjectNode, GenericSink):
    """Accumulate input waveform peaks and offer bounded LCD snapshots."""

    folder = "../nodes_impl"

    def __init__(self, name, samples):
        super().__init__(name, identified=True, selectors=["waveform"])
        self.addInput("i", CType(F32), sample_count(samples))
        self.addEventOutput()

# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from ._base import ProjectNode, sample_count

class SlidingBuffer(ProjectNode, GenericNode):
    """Consume hop F32 audio samples; produce frame_samples F32 history samples.

    Retains only frame_samples - hop overlap samples, initially zero. Each
    firing appends the new hop and emits the full time-domain frame, including
    zero padding during startup.
    """
    def __init__(self, name, hop, frame_samples):
        GenericNode.__init__(self, name, identified=False)
        hop, frame_samples = sample_count(hop), sample_count(frame_samples)
        if hop > frame_samples:
            raise ValueError("SlidingBuffer hop must not exceed the frame length")
        self.addInput("i", CType(F32), hop)
        self.addOutput("o", CType(F32), frame_samples)

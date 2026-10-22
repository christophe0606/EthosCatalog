# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import *
from common.nodes._base import ProjectNode


class HandwritingSynthesis(ProjectNode, BaseNode):
    """Own the sentence recurrence and coordinate one stroke through five nodes."""
    folder = '../nodes_impl'
    needsHardware = True

    def __init__(self, name):
        BaseNode.__init__(self, name, identified=False,
                          selectors=['request', 'ack', 'stroke', 'advance'])
        self.addEventInput(2)  # sentence request, completed model chain
        self.addEventOutput(3)  # acknowledgment, display, first model
        self.parameters()

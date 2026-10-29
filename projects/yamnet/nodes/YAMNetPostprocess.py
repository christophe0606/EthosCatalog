# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""CPU score validation, top-five selection and class-label lookup."""
from cmsis_stream.cg.scheduler import BaseNode
from common.nodes._base import ProjectNode


class YAMNetPostprocess(ProjectNode, BaseNode):
    folder = '../nodes_impl'

    def __init__(self, name):
        BaseNode.__init__(self, name, identified=True,
                          selectors=['scores', 'result', 'complete'])
        self.addEventInput()
        self.addEventOutput(2)

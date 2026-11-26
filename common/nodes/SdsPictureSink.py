# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import BaseNode
from ._base import ProjectNode


class SdsPictureSink(ProjectNode, BaseNode):
    """Record each picture(timeslot, BufferPtr) event as one SDS record.

    `params` is the C++ expression for the application's SdsPictureSinkParams.
    """

    def __init__(self, name, params, *, identified=False):
        super().__init__(name, identified=identified, selectors=["picture"])
        self.addEventInput()
        self.addVariableArg(params)

# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import BaseNode
from ._base import ProjectNode


class SdsPictureSource(ProjectNode, BaseNode):
    """Read one SDS picture on kDo and emit picture(timeslot, BufferPtr).

    The second output emits pictureEnd, pictureError, and pictureDropped.
    `params` is the C++ expression for the application's SdsPictureSourceParams.
    """

    def __init__(self, name, params, *, identified=False):
        super().__init__(name, identified=identified,
                         selectors=["picture", "pictureEnd", "pictureError", "pictureDropped"])
        self.addEventInput()
        self.addEventOutput(2)
        self.addVariableArg(params)

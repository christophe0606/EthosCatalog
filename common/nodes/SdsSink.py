# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import GenericSink
from ._base import ProjectNode, sample_count


class SdsSink(ProjectNode, GenericSink):
    """Record `samples` values of `the_type` as one SDS record per firing.

    `params` is a C++ expression for the application's SdsSinkParams.
    """

    def __init__(self, name, the_type, samples, params):
        super().__init__(name, identified=False)
        self.addInput("i", the_type, sample_count(samples))
        self.addVariableArg(params)

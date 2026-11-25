# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from cmsis_stream.cg.scheduler import GenericSource
from ._base import ProjectNode, sample_count


class SdsSource(ProjectNode, GenericSource):
    """Play one SDS record per firing as `samples` values of `the_type`.

    `params` is a C++ expression for the application's SdsSourceParams.
    """

    def __init__(self, name, the_type, samples, params, *, identified=False):
        super().__init__(name, identified=identified)
        self.addOutput("o", the_type, sample_count(samples))
        self.addVariableArg(params)

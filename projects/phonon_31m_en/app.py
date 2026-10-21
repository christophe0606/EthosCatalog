# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Reserved CMSIS Stream entry point; only the hardware benchmark is implemented."""
from cmsis_stream.cg.scheduler import Graph


def make_graph() -> Graph:
    raise NotImplementedError(
        'Phonon UART-to-audio graph is not implemented: hardware feasibility '
        'is still failing. See CHECKPOINT.md.'
    )


def generate():
    return make_graph()


if __name__ == '__main__':
    generate()

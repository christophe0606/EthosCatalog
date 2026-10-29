# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Reserved CMSIS Stream entry point; only the hardware benchmark is implemented."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from cmsis_stream.cg.scheduler import Graph
from common.stream_graph import graph_only_requested


def make_graph() -> Graph:
    raise NotImplementedError(
        'Phonon UART-to-audio graph is not implemented: hardware feasibility '
        'is still failing. See CHECKPOINT.md.'
    )


def generate(graph_only=False):
    # Accept the shared CLI option; there are no nodes or diagrams to style yet.
    return make_graph()


if __name__ == '__main__':
    generate(graph_only=graph_only_requested())

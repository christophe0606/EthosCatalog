# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Generate the UART / event inference / animated handwriting graph."""
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from cmsis_stream.cg.scheduler import Graph
from common.stream_graph import DRIVER_COLOR, GraphStyle, graph_only_requested, mk_app
from projects.handwriting_synthesis.nodes import *

# Fixed color for this project's ai_layer/model.pte; all its methods share it.
# Keep this assignment stable and distinct from the other projects' PTE colors.
PTE_COLOR = '#AD1457'
GRAPH_STYLE = GraphStyle({
    **dict.fromkeys(('lstm1', 'attention', 'lstm2', 'lstm3', 'gmm'), PTE_COLOR),
    **dict.fromkeys(('uart', 'display'), DRIVER_COLOR),
})


def make_graph():
    graph = Graph()
    uart, editor, send = UartSource('uart'), SentenceInput('editor'), SendToNetwork('send')
    synthesis, display = HandwritingSynthesis('synthesis'), HandwritingDisplay('display')
    networks = [HandwritingNetwork(name, method) for method, name in enumerate(
        ('lstm1', 'attention', 'lstm2', 'lstm3', 'gmm'))]
    graph.connect(uart.o, editor.i)
    graph.connect(editor.o, send.i)
    graph.connect(send['oev0'], synthesis['iev0'])
    graph.connect(synthesis['oev0'], send['iev0'])
    graph.connect(synthesis['oev1'], display['iev0'])
    graph.connect(synthesis['oev2'], networks[0]['iev0'])
    for source, destination in zip(networks, networks[1:]):
        graph.connect(source['oev0'], destination['iev0'])
    graph.connect(networks[-1]['oev0'], synthesis['iev1'])
    return graph

def generate(graph_only=False):
    return mk_app(make_graph(), Path(__file__).resolve().parent/'generated',
        style=GRAPH_STYLE, graph_only=graph_only,
        globals=dict(MAX_CHARACTERS=75, LOGICAL_WIDTH=800, LOGICAL_HEIGHT=480), runtime=dict(
        CMSISSTREAM_EVENT_QUEUE_LENGTH=4, CMSISSTREAM_EVT_THREAD_STACK_SIZE=16384,
        CMSISSTREAM_STREAM_THREAD_STACK_SIZE=8192,
        CMSISSTREAM_STREAM_THREAD_PRIORITY='osPriorityRealtime',
        CMSISSTREAM_EVT_NORMAL_PRIORITY='osPriorityHigh',
        CMSISSTREAM_NB_MAX_EVENTS=8, CMSISSTREAM_NB_MAX_BUFS=4),
        source_notice="// Copyright 2026 Arm Limited and/or its affiliates.\n// SPDX-License-Identifier: Apache-2.0\n")
if __name__ == '__main__': generate(graph_only=graph_only_requested())

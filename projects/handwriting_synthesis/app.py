# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Generate the UART / event inference / animated handwriting graph."""
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from cmsis_stream.cg.scheduler import Graph
from common.stream_graph import mk_app
from projects.handwriting_synthesis.nodes import *
def make_graph():
    graph = Graph()
    uart, editor, send = UartSource('uart'), SentenceInput('editor'), SendToNetwork('send')
    network, display = HandwritingNetwork('network'), HandwritingDisplay('display')
    graph.connect(uart.o, editor.i)
    graph.connect(editor.o, send.i)
    graph.connect(send['oev0'], network['iev0'])
    graph.connect(network['oev0'], send['iev0'])
    graph.connect(network['oev1'], display['iev0'])
    return graph

def generate():
    return mk_app(make_graph(), Path(__file__).resolve().parent/'generated',
        globals=dict(MAX_CHARACTERS=75, LOGICAL_WIDTH=800, LOGICAL_HEIGHT=480), runtime=dict(
        CMSISSTREAM_EVENT_QUEUE_LENGTH=4, CMSISSTREAM_EVT_THREAD_STACK_SIZE=16384,
        CMSISSTREAM_STREAM_THREAD_STACK_SIZE=8192,
        CMSISSTREAM_STREAM_THREAD_PRIORITY='osPriorityRealtime',
        CMSISSTREAM_EVT_NORMAL_PRIORITY='osPriorityHigh',
        CMSISSTREAM_NB_MAX_EVENTS=8, CMSISSTREAM_NB_MAX_BUFS=4),
        source_notice="// Copyright 2026 Arm Limited and/or its affiliates.\n// SPDX-License-Identifier: Apache-2.0\n")
if __name__ == '__main__': generate()

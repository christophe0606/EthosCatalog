# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Generate the live classifier from the repository root or this directory."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from cmsis_stream.cg.scheduler import Graph
from common.nodes import MicrophoneSource, PCMToFloat, LogMel
from common.stream_graph import DRIVER_COLOR, GraphStyle, graph_only_requested, mk_app
from projects.yamnet.nodes import AudioWindow, SendToNetwork, YAMNet, YAMNetPostprocess, EventLCD

# Fixed color for the one forward method in this project's PTE.
STYLE = GraphStyle({
    'network': '#00796B',
    'microphone': DRIVER_COLOR,
    'lcd': DRIVER_COLOR,
})


def make_graph():
    graph = Graph()
    microphone = MicrophoneSource('microphone', 160)
    pcm = PCMToFloat('pcm', 160)
    window = AudioWindow('audio_window')
    mel = LogMel('log_mel', 400, 64, 'yamnet_log_mel_config')
    publisher = SendToNetwork('send')
    network = YAMNet('network')
    postprocess = YAMNetPostprocess('postprocess')
    lcd = EventLCD('lcd')
    stages = [microphone, pcm, window, mel, publisher]
    for source, destination in zip(stages, stages[1:]):
        graph.connect(source.o, destination.i)
    graph.connect(publisher['oev0'], network['iev0'])
    graph.connect(network['oev0'], publisher['iev0'])
    graph.connect(network['oev1'], postprocess['iev0'])
    graph.connect(postprocess['oev0'], lcd['iev0'])
    # Release the in-flight request after postprocessing has consumed its scores.
    graph.connect(postprocess['oev1'], network['iev1'])
    return graph


def generate(graph_only=False):
    return mk_app(
        make_graph(), Path(__file__).resolve().parent / 'generated',
        style=STYLE, graph_only=graph_only,
        source_notice="// Copyright 2026 Arm Limited and/or its affiliates.\n// SPDX-License-Identifier: Apache-2.0\n",
        globals=dict(MIC_BLOCK_SAMPLES=160, MIC_SAMPLE_RATE=16000),
        runtime=dict(
            CMSISSTREAM_EVENT_QUEUE_LENGTH=8,
            CMSISSTREAM_EVT_THREAD_STACK_SIZE=16384,
            CMSISSTREAM_STREAM_THREAD_STACK_SIZE=32768,
            CMSISSTREAM_STREAM_THREAD_PRIORITY='osPriorityRealtime',
            CMSISSTREAM_EVT_NORMAL_PRIORITY='osPriorityHigh',
            CMSISSTREAM_NB_MAX_EVENTS=12,
            CMSISSTREAM_NB_MAX_BUFS=4,
        ),
    )


if __name__ == '__main__':
    generate(graph_only=graph_only_requested())

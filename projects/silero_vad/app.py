# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Generate the live16k Silero microphone/LCD graph from root or project."""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from cmsis_stream.cg.scheduler import Graph
from common.nodes import MicrophoneSource,PCMToFloat
from common.stream_graph import DRIVER_COLOR,GraphStyle,graph_only_requested,mk_app
from projects.silero_vad.nodes import SileroInference,VADDisplayClock,VADLCD
PTE_COLOR='#00695C'
GRAPH_STYLE=GraphStyle({'inference':PTE_COLOR,'microphone':DRIVER_COLOR,'lcd':DRIVER_COLOR})
def make_graph():
    g=Graph()
    mic=MicrophoneSource('microphone',512);pcm=PCMToFloat('pcm',512)
    vad=SileroInference('inference');clock=VADDisplayClock('display_clock');lcd=VADLCD('lcd')
    g.connect(mic.o,pcm.i);g.connect(pcm.o,vad.i);g.connect(vad.o,clock.i)
    g.connect(clock['oev0'],lcd['iev0'])
    return g
def generate(graph_only=False):
    return mk_app(make_graph(),Path(__file__).resolve().parent/'generated',style=GRAPH_STYLE,graph_only=graph_only,
      globals=dict(MIC_BLOCK_SAMPLES=512,MIC_SAMPLE_RATE=16000,LCD_FPS=20),runtime=dict(
       CMSISSTREAM_EVENT_QUEUE_LENGTH=4,CMSISSTREAM_EVT_THREAD_STACK_SIZE=8192,
       CMSISSTREAM_STREAM_THREAD_STACK_SIZE=32768,CMSISSTREAM_STREAM_THREAD_PRIORITY='osPriorityRealtime',
       CMSISSTREAM_EVT_NORMAL_PRIORITY='osPriorityHigh',CMSISSTREAM_NB_MAX_EVENTS=12,CMSISSTREAM_NB_MAX_BUFS=4))
if __name__=='__main__':generate(graph_only=graph_only_requested())

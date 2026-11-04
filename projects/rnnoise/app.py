# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from cmsis_stream.cg.scheduler import Graph
from common.nodes import MicrophoneSource,RNNoiseAnalysis,RNNoiseSynthesis
from common.stream_graph import DRIVER_COLOR,GraphStyle,graph_only_requested,mk_app
from projects.rnnoise.nodes import RNNoiseInference,RNNoiseMetrics,RNNoiseLCD
STYLE=GraphStyle({'inference':'#7B1FA2','microphone':DRIVER_COLOR,'lcd':DRIVER_COLOR})
SOURCE_NOTICE = (
    '// Copyright 2026 Arm Limited and/or its affiliates.\n'
    '// SPDX-License-Identifier: Apache-2.0\n'
)
def make_graph():
 g=Graph();mic=MicrophoneSource('microphone',480);a=RNNoiseAnalysis('analysis');n=RNNoiseInference('inference');s=RNNoiseSynthesis('synthesis');sink=RNNoiseMetrics('null_metrics');lcd=RNNoiseLCD('lcd')
 g.connect(mic.o,a.i);g.connect(a.o1,n.i);g.connect(a.o2,s.i1);g.connect(n.o,s.i2);g.connect(s.o,sink.i);g.connect(sink['oev0'],lcd['iev0']);return g
def generate(graph_only=False):
 return mk_app(make_graph(),Path(__file__).resolve().parent/'generated',style=STYLE,source_notice=SOURCE_NOTICE,graph_only=graph_only,globals=dict(MIC_BLOCK_SAMPLES=480,MIC_SAMPLE_RATE=48000,LCD_FPS=10),runtime=dict(CMSISSTREAM_EVENT_QUEUE_LENGTH=3,CMSISSTREAM_EVT_THREAD_STACK_SIZE=12288,CMSISSTREAM_STREAM_THREAD_STACK_SIZE=65536,CMSISSTREAM_STREAM_THREAD_PRIORITY='osPriorityRealtime',CMSISSTREAM_EVT_NORMAL_PRIORITY='osPriorityHigh',CMSISSTREAM_NB_MAX_EVENTS=8,CMSISSTREAM_NB_MAX_BUFS=4))
if __name__=='__main__':generate(graph_only=graph_only_requested())

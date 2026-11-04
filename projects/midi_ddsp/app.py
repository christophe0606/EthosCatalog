# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from cmsis_stream.cg.scheduler import Graph
from common.stream_graph import GraphStyle,DRIVER_COLOR,mk_app,graph_only_requested
from projects.midi_ddsp.nodes import *

def make_graph():
 g=Graph()
 score=MidiScore('score');send=SendToNetwork('send')
 g.connect(score.o,send.i)
 for k,name in enumerate(('expression_forward','expression_backward','expression_step','precondition','context_forward','context_backward','pitch_step','harmonic')):
  method=MidiMethod(name,k)
  g.connect(send[f'oev{k}'],method['iev0'])
  g.connect(method['oev0'],send[f'iev{k*2}'])
  g.connect(method['oev1'],send[f'iev{k*2+1}'])
 control=ControlSource('controls');g.connect(send.o,control.i);synth=DDSPSynth('synth');reverb=DDSPReverb('reverb');audio=AudioOutput('audio')
 for a,b in ((control,synth),(synth,reverb),(reverb,audio)):g.connect(a.o,b.i)
 return g

def generate(graph_only=False):
 methods=('expression_forward','expression_backward','expression_step','precondition','context_forward','context_backward','pitch_step','harmonic')
 return mk_app(make_graph(),Path(__file__).resolve().parent/'generated',globals=dict(MIDI_REGION_LIMIT=16,MIDI_AUDIO_RATE=16000,MIDI_AUDIO_BLOCK=256),runtime=dict(
 CMSISSTREAM_EVENT_QUEUE_LENGTH=8,CMSISSTREAM_EVT_THREAD_STACK_SIZE=16384,CMSISSTREAM_STREAM_THREAD_STACK_SIZE=16384,
 CMSISSTREAM_STREAM_THREAD_PRIORITY='osPriorityRealtime',CMSISSTREAM_EVT_NORMAL_PRIORITY='osPriorityHigh',CMSISSTREAM_NB_MAX_EVENTS=24,CMSISSTREAM_NB_MAX_BUFS=16),
 source_notice='// Copyright 2026 Arm Limited and/or its affiliates.\n// SPDX-License-Identifier: Apache-2.0\n',
 style=GraphStyle({**dict.fromkeys(methods,'#6A1B9A'),'audio':DRIVER_COLOR}),graph_only=graph_only)
if __name__=='__main__':generate(graph_only_requested())

# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""UART to whole-utterance hybrid FARGAN speech, independent clocked audio and LCD flow."""
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from cmsis_stream.cg.scheduler import Graph
from common.stream_graph import mk_app
from projects.rvtts_fargan.nodes import *

def make_graph():
    g = Graph()
    uart, editor, send = UartSource('uart'), LineInput('editor'), SendToNetwork('send')
    frontend = TextFrontend('frontend')
    nets = [RvttsNetwork(name, i) for i, name in enumerate(('pre', 'gru', 'head', 'acoustic'))]
    pitch, voc, pcm = PitchCorrect('pitch'), FarganVocoder('vocoder'), PlaybackSource('pcm')
    speaker, wave, lcd = I2SSink('speaker'), WaveformSink('wave'), WaveformDisplay('lcd')
    g.connect(uart.o, editor.i); g.connect(editor.o, send.i); g.connect(send.o, pcm.i)
    g.connect(send['oev0'], frontend['iev0']); g.connect(frontend['oev1'], send['iev0'])
    chain = [frontend, *nets, pitch, voc, pcm]
    for a, b in zip(chain, chain[1:]): g.connect(a['oev0'], b['iev0'])
    g.connect(pcm['oev0'], send['iev0'])
    g.connect(pcm.o, speaker.i); g.connect(pcm.o, wave.i)
    g.connect(wave['oev0'], lcd['iev0'])
    return g

def generate():
    return mk_app(make_graph(), Path(__file__).resolve().parent/'generated',
        globals=dict(MAX_TEXT=160, AUDIO_FRAME=160), runtime=dict(
        CMSISSTREAM_EVENT_QUEUE_LENGTH=8, CMSISSTREAM_EVT_THREAD_STACK_SIZE=32768,
        CMSISSTREAM_STREAM_THREAD_STACK_SIZE=8192,
        CMSISSTREAM_STREAM_THREAD_PRIORITY='osPriorityRealtime',
        CMSISSTREAM_EVT_NORMAL_PRIORITY='osPriorityHigh',
        CMSISSTREAM_NB_MAX_EVENTS=16, CMSISSTREAM_NB_MAX_BUFS=4),
        source_notice='// Copyright 2026 Arm Limited and/or its affiliates.\n// SPDX-License-Identifier: Apache-2.0\n')
if __name__ == '__main__': generate()

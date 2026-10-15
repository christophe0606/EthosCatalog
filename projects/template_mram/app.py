# SPDX-License-Identifier: Apache-2.0
"""Generate from the solution root or this folder: uv run app.py."""
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'common'))
from cmsis_stream.cg.scheduler import Graph, CStructType
from nodes import MicrophoneSource, PCMToFloat, EmbeddedGain, SlidingBuffer, RemoveDC, HannWindow, FFT, Magnitude, SendToLCD, LCD
from stream_graph import mk_app

BLOCK, FFT_SIZE, SAMPLE_RATE, FPS = 512, 2048, 48000, 20
PCM = CStructType('PcmBlock', 16 + 2*BLOCK)
AUDIO = CStructType('AudioBlock', 16 + 4*BLOCK)
FRAME = CStructType('FFTBlock', 16 + 4*FFT_SIZE)
SPECTRUM = CStructType('SpectrumBlock', 16 + 4*(FFT_SIZE//2+1))

def make_graph():
    graph = Graph()
    stages = [MicrophoneSource('microphone', PCM), PCMToFloat('pcm', PCM, AUDIO),
              EmbeddedGain('gain', AUDIO), SlidingBuffer('sliding', AUDIO, FRAME),
              RemoveDC('dc', FRAME), HannWindow('window', FRAME), FFT('fft', FRAME),
              Magnitude('magnitude', FRAME, SPECTRUM),
              SendToLCD('send', SPECTRUM, SAMPLE_RATE, BLOCK, FPS)]
    lcd = LCD('lcd', FFT_SIZE, SAMPLE_RATE, FPS)
    for src, dst in zip(stages, stages[1:]):
        graph.connect(src.o, dst.i)
    graph.connect(stages[-1]['oev0'], lcd['iev0'])
    return graph

def generate():
    output = Path(__file__).resolve().parent/'generated'
    return mk_app(make_graph(), output, globals=dict(
        MIC_BLOCK_SAMPLES=BLOCK, MIC_SAMPLE_RATE=SAMPLE_RATE,
        SPECTRUM_FFT_SIZE=FFT_SIZE, SPECTRUM_BINS=FFT_SIZE//2+1, LCD_FPS=FPS), runtime=dict(
        CMSISSTREAM_EVENT_QUEUE_LENGTH=3,
        CMSISSTREAM_EVT_THREAD_STACK_SIZE=8192,
        CMSISSTREAM_STREAM_THREAD_STACK_SIZE=32768,
        CMSISSTREAM_STREAM_THREAD_PRIORITY='osPriorityRealtime',
        CMSISSTREAM_EVT_NORMAL_PRIORITY='osPriorityHigh',
        CMSISSTREAM_NB_MAX_EVENTS=8,
        CMSISSTREAM_NB_MAX_BUFS=4))

if __name__ == '__main__':
    generate()

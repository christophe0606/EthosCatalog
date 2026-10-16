# SPDX-License-Identifier: Apache-2.0
"""Generate from the solution root or this folder: uv run app.py."""
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from cmsis_stream.cg.scheduler import Graph
from common.nodes import MicrophoneSource, PCMToFloat, SlidingBuffer, RemoveDC, HannWindow, FFT, RFFTMagnitude
from common.stream_graph import mk_app
from projects.template_mram.nodes import EthosGain, SendToLCD, LCD

BLOCK, FFT_SIZE, SAMPLE_RATE, FPS = 512, 2048, 48000, 20
# Dataflow ports carry scalar samples. Their meanings are documented by each
# Python node: signed PCM, normalized audio, packed real FFT, or magnitude bins.
# Equal scalar types do not imply compatible meanings or frame boundaries.
# The board backend enables PDM channel 4 only: mono signed 16-bit PCM at 48 kHz.
# A stereo backend would require a stereo sample type and conversion to mono
# before this graph's mono audio/FFT processing.

def make_graph(pcm_samples=BLOCK):
    graph = Graph()
    stages = [MicrophoneSource('microphone', BLOCK), PCMToFloat('pcm', pcm_samples),
              EthosGain('gain', BLOCK), SlidingBuffer('sliding', BLOCK, FFT_SIZE),
              RemoveDC('dc', FFT_SIZE), HannWindow('window', FFT_SIZE), FFT('fft', FFT_SIZE),
              RFFTMagnitude('magnitude', FFT_SIZE, amplitude_scale=4.0/FFT_SIZE),
              SendToLCD('send', FFT_SIZE//2+1, SAMPLE_RATE, BLOCK, FPS)]
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

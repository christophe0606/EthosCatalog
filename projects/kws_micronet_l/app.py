# SPDX-License-Identifier: Apache-2.0
"""Generate the KWS demo from the repository root or this directory."""
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from cmsis_stream.cg.scheduler import Graph
from common.nodes import MicrophoneSource, PCMToFloat, SlidingBuffer, MFCC
from common.stream_graph import mk_app
from projects.kws_micronet_l.nodes import KwsInference, KwsEvents, KeywordLCD

SAMPLE_RATE, AUDIO_HOP, FRAME, FEATURES, CONTEXT = 16000, 320, 640, 10, 49
# 49-24 = 25 new MFCC blocks -> 500 ms, matching KwsPreProcess.
# Set overlap to 48 for inference every new MFCC block (20 ms).
MFCC_OVERLAP = 24

def make_graph(overlap=MFCC_OVERLAP):
    if isinstance(overlap, bool) or not isinstance(overlap, int) or not 0 <= overlap < CONTEXT:
        raise ValueError('MFCC overlap must be between 0 and 48')
    graph = Graph()
    mic = MicrophoneSource('microphone', AUDIO_HOP)
    pcm = PCMToFloat('pcm', AUDIO_HOP)
    audio = SlidingBuffer('audio_window', AUDIO_HOP, FRAME)
    mfcc = MFCC('mfcc', FRAME, FEATURES, 'kws_mfcc_config')
    context = SlidingBuffer('mfcc_window', (CONTEXT-overlap)*FEATURES, CONTEXT*FEATURES)
    inference = KwsInference('inference', CONTEXT*FEATURES)
    events = KwsEvents('display_clock', FEATURES)
    lcd = KeywordLCD('lcd')
    stages = [mic, pcm, audio, mfcc, context, inference]
    for source, destination in zip(stages, stages[1:]):
        graph.connect(source.o, destination.i)
    graph.connect(mfcc.o, events.i)
    graph.connect(events['oev0'], lcd['iev0'])
    graph.connect(inference['oev0'], lcd['iev0'])
    return graph

def generate():
    return mk_app(make_graph(), Path(__file__).resolve().parent/'generated', globals=dict(
        MIC_BLOCK_SAMPLES=AUDIO_HOP, MIC_SAMPLE_RATE=SAMPLE_RATE, KWS_FRAME_SAMPLES=FRAME,
        KWS_FEATURES=FEATURES, KWS_CONTEXT_FRAMES=CONTEXT, KWS_MFCC_OVERLAP=MFCC_OVERLAP,
        KWS_INFERENCE_HOP_FRAMES=CONTEXT-MFCC_OVERLAP, LCD_FPS=20), runtime=dict(
        CMSISSTREAM_EVENT_QUEUE_LENGTH=4, CMSISSTREAM_EVT_THREAD_STACK_SIZE=8192,
        CMSISSTREAM_STREAM_THREAD_STACK_SIZE=32768,
        CMSISSTREAM_STREAM_THREAD_PRIORITY='osPriorityRealtime',
        CMSISSTREAM_EVT_NORMAL_PRIORITY='osPriorityHigh', CMSISSTREAM_NB_MAX_EVENTS=12,
        CMSISSTREAM_NB_MAX_BUFS=4))

if __name__ == '__main__':
    generate()

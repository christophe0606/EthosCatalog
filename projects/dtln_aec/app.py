# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Generate the DTLN-aec demo scheduler and graph."""

from pathlib import Path
import sys

PROJECT = Path(__file__).resolve().parent
sys.path.insert(0, str(PROJECT.parents[1]))

from cmsis_stream.cg.scheduler import CType, F32, Graph
from common.stream_graph import GraphStyle, configuration, graph_only_requested, mk_app
from common.nodes import (SlidingBuffer, FFT, PreservingFFT, RawRFFTMagnitude,
                          RFFTMaskInverse, OverlapAdd, SdsSource, SdsSink)
from projects.dtln_aec.nodes import DtlnMethod, DtlnSplitInput, SpeakerOutput, WaveformCapture, WaveformDisplay

SAMPLE_RATE = 16000
HOP_SAMPLES = 128
NOTICE = (
    "// Copyright 2026 Arm Limited and/or its affiliates.\n"
    "// SPDX-License-Identifier: Apache-2.0\n"
)


def make_graph():
    graph = Graph()
    audio = CType(F32)
    source = SdsSource("sds_source", audio, 2 * HOP_SAMPLES, "dtlnReplayParams", identified=True)
    split = DtlnSplitInput("split_input", HOP_SAMPLES)
    mic_window = SlidingBuffer("mic_window", HOP_SAMPLES, 512, identified=True)
    ref_window = SlidingBuffer("reference_window", HOP_SAMPLES, 512, identified=True)
    mic_fft = FFT("mic_fft", 512)
    ref_fft = PreservingFFT("reference_fft")
    mic_magnitude = RawRFFTMagnitude("mic_magnitude", 512)
    ref_magnitude = RawRFFTMagnitude("reference_magnitude", 512)
    stage1 = DtlnMethod("stage1", 1)
    inverse = RFFTMaskInverse("mask_inverse", 512)
    stage2 = DtlnMethod("stage2", 2)
    synthesis = OverlapAdd("overlap_add", 512, HOP_SAMPLES, identified=True)
    sink = SdsSink("sds_sink", audio, HOP_SAMPLES, "dtlnRecordParams", identified=True)
    waveform = WaveformCapture("waveform", HOP_SAMPLES)
    lcd = WaveformDisplay("lcd")
    speaker = SpeakerOutput("speaker", HOP_SAMPLES)
    graph.connect(source.o, split.i)
    graph.connect(split.o1, mic_window.i)
    graph.connect(split.o2, ref_window.i)
    graph.connect(mic_window.o, mic_fft.i)
    graph.connect(ref_window.o, ref_fft.i)
    graph.connect(mic_fft.o, mic_magnitude.i)
    graph.connect(ref_fft.o, ref_magnitude.i)
    graph.connect(mic_magnitude.o, stage1.i1)
    graph.connect(ref_magnitude.o, stage1.i2)
    graph.connect(mic_fft.o, inverse.i1)
    graph.connect(stage1.o, inverse.i2)
    graph.connect(inverse.o, stage2.i1)
    graph.connect(ref_window.o, stage2.i2)
    graph.connect(stage2.o, synthesis.i)
    graph.connect(synthesis.o, sink.i)
    graph.connect(split.o1, waveform.i1)
    graph.connect(synthesis.o, waveform.i2)
    graph.connect(waveform["oev0"], lcd["iev0"])
    graph.connect(synthesis.o, speaker.i)
    return graph


def generate(graph_only=False):
    style = GraphStyle({"stage1": "#7B1FA2", "stage2": "#7B1FA2"})
    config = configuration()
    config.appConfigCName = "dtln_app_config.hpp"
    return mk_app(
        make_graph(),
        PROJECT / "generated",
        globals={"DTLN_AUDIO_SAMPLE_RATE": SAMPLE_RATE, "DTLN_AUDIO_HOP_SAMPLES": HOP_SAMPLES,
                 "DTLN_AUDIO_FRAME_SAMPLES": 512},
        runtime={
            "CMSISSTREAM_EVT_THREAD_STACK_SIZE": 4096,
            "CMSISSTREAM_EVENT_QUEUE_LENGTH": 4,
            "CMSISSTREAM_EVT_LOW_PRIORITY": "osPriorityBelowNormal",
            "CMSISSTREAM_STREAM_THREAD_STACK_SIZE": 24576,
            "CMSISSTREAM_STREAM_THREAD_PRIORITY": "osPriorityNormal",
        },
        config=config,
        style=style,
        source_notice=NOTICE,
        graph_only=graph_only,
    )


if __name__ == "__main__":
    generate(graph_only=graph_only_requested())

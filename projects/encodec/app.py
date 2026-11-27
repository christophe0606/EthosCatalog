# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Generate the EnCodec SDS replay scheduler and graph."""

from pathlib import Path
import sys

PROJECT = Path(__file__).resolve().parent
sys.path.insert(0, str(PROJECT.parents[1]))

from cmsis_stream.cg.scheduler import CType, F32, Graph
from common.nodes import SdsSink, SdsSource
from common.stream_graph import DRIVER_COLOR, GraphStyle, configuration, graph_only_requested, mk_app
from projects.encodec.nodes import EnCodecDecoder, EnCodecEncoder, WaveformCapture, WaveformDisplay

SAMPLE_RATE = 24000
BLOCK_SAMPLES = 2880
BLOCK_CODES = 8 * 9
NOTICE = (
    "// Copyright 2026 Arm Limited and/or its affiliates.\n"
    "// SPDX-License-Identifier: Apache-2.0\n"
)


def make_graph():
    graph = Graph()
    audio = CType(F32)
    source = SdsSource("sds_source", audio, BLOCK_SAMPLES, "encodecReplayParams", identified=True)
    encoder = EnCodecEncoder("encoder", BLOCK_SAMPLES, BLOCK_CODES)
    decoder = EnCodecDecoder("decoder", BLOCK_CODES, BLOCK_SAMPLES)
    sink = SdsSink("sds_sink", audio, BLOCK_SAMPLES, "encodecRecordParams", identified=True)
    capture = WaveformCapture("waveform", BLOCK_SAMPLES)
    display = WaveformDisplay("lcd")
    graph.connect(source.o, encoder.i)
    graph.connect(encoder.o, decoder.i)
    graph.connect(decoder.o, sink.i)
    graph.connect(source.o, capture.i)
    graph.connect(capture["oev0"], display["iev0"])
    return graph


def generate(graph_only=False):
    config = configuration()
    config.appConfigCName = "encodec_app_config.hpp"
    return mk_app(
        make_graph(),
        PROJECT / "generated",
        globals={"ENCODEC_SAMPLE_RATE": SAMPLE_RATE, "ENCODEC_BLOCK_SAMPLES": BLOCK_SAMPLES},
        runtime={
            "CMSISSTREAM_EVT_THREAD_STACK_SIZE": 4096,
            "CMSISSTREAM_EVENT_QUEUE_LENGTH": 4,
            "CMSISSTREAM_EVT_LOW_PRIORITY": "osPriorityBelowNormal",
            "CMSISSTREAM_STREAM_THREAD_STACK_SIZE": 24576,
            "CMSISSTREAM_STREAM_THREAD_PRIORITY": "osPriorityNormal",
        },
        config=config,
        style=GraphStyle({"encoder": "#3A6088", "decoder": "#3A6088", "lcd": DRIVER_COLOR}),
        source_notice=NOTICE,
        graph_only=graph_only,
    )


if __name__ == "__main__":
    generate(graph_only=graph_only_requested())

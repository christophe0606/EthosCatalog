# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Generate CMSIS Stream capture, grayscale, inference, decode and LCD graph."""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from cmsis_stream.cg.scheduler import Graph
from common.stream_graph import mk_app
from projects.object_detection.nodes import CameraCapture, FaceInference, FaceDecode, FaceDisplay
from common.nodes import Rgb565CenterCrop, Rgb888ToGrayInt8, NonMaximumSuppression

def make_graph():
    graph=Graph()
    camera=CameraCapture('camera')
    crop=Rgb565CenterCrop('crop',192,192)
    gray=Rgb888ToGrayInt8('gray',192*192)
    network=FaceInference('network')
    decode=FaceDecode('decode')
    nms=NonMaximumSuppression('nms',540,.45)
    display=FaceDisplay('display')
    graph.connect(camera.o,crop.i)
    graph.connect(crop.o,gray.i)
    graph.connect(crop.o,network.rgb)
    graph.connect(gray.o,network.i)
    graph.connect(network.o,decode.i)
    graph.connect(decode.boxes,nms.i)
    graph.connect(decode.frame,display.frame)
    graph.connect(nms.o,display.boxes)
    return graph

def generate():
    return mk_app(make_graph(),Path(__file__).resolve().parent/'generated',globals=dict(DETECTION_IMAGE_SIZE=192,DETECTION_FRAME_PERIOD_MS=200),runtime=dict(
        CMSISSTREAM_EVENT_QUEUE_LENGTH=3,
        CMSISSTREAM_EVT_THREAD_STACK_SIZE=4096,
        CMSISSTREAM_STREAM_THREAD_STACK_SIZE=16384,
        CMSISSTREAM_STREAM_THREAD_PRIORITY='osPriorityHigh',
        CMSISSTREAM_EVT_NORMAL_PRIORITY='osPriorityAboveNormal',
        CMSISSTREAM_NB_MAX_EVENTS=8,
        CMSISSTREAM_NB_MAX_BUFS=4),source_notice='// Copyright 2026 Arm Limited and/or its affiliates.\n// SPDX-License-Identifier: Apache-2.0\n')

if __name__=='__main__': generate()

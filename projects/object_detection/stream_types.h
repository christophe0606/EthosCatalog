// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#include "../../common/nodes_impl/c/ImageTypes.h"
#include "../../common/nodes_impl/c/DetectionTypes.h"
typedef struct {
    volatile int32_t error;
    volatile uint32_t captured, preprocessed, inferences, decoded, frames_presented;
    volatile uint32_t detections, last_detections, frame_ticks_max, processing_ticks_max;
    volatile uint32_t frame_deadline_misses, capture_timeouts, camera_errors;
    volatile uint32_t dropped_frames, dropped_events, ownership_errors;
    volatile uint32_t frame_started, last_frame_ticks, forward_ticks_max;
    volatile uint32_t cropped, grayscaled, nms;
} StreamStats;
typedef Rgb565Image CameraFrame;
typedef struct { const uint8_t *rgb; const int8_t *input; uint32_t sequence, started; } PreparedFrame;
typedef struct { PreparedFrame frame; const int8_t *head0, *head1; } InferredFrame;
typedef ScoredBox FaceBox;
typedef struct { PreparedFrame frame; const FaceBox *boxes; uint32_t count; } DetectedFrame;

// Copyright (c) 2022 Arm Limited. All rights reserved.
// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// One-class translation of MLEK DetectorPostProcessing, see model/NOTICE.md.
#include "FaceDecode.hpp"
#include "../../../common/nodes_impl/c/NonMaximumSuppression.hpp"
#include <math.h>
static float sigmoid(float x) { return 1.0f/(1.0f+expf(-x)); }
uint32_t face_decode_candidates(const int8_t *head0,const int8_t *head1,FaceBox *out)
{
    static const float scales[2]={0.13408391177654266f,0.18535925447940826f};
    static const int zero[2]={47,10};
    static const float anchors[2][3][2]={{{38,77},{47,97},{61,126}},{{14,26},{19,37},{28,55}}};
    uint32_t count=0;
    for (unsigned h=0;h<2;++h) {
        const int8_t *head=h?head1:head0; unsigned grid=h?12:6;
        for(unsigned y=0;y<grid;++y) for(unsigned x=0;x<grid;++x) for(unsigned a=0;a<3;++a) {
            const int8_t *q=head+((y*grid+x)*3+a)*6;
            float obj=sigmoid((q[4]-zero[h])*scales[h]);
            float score=obj*sigmoid((q[5]-zero[h])*scales[h]);
            if(obj<=.5f || score<=.5f) continue;
            float cx=(sigmoid((q[0]-zero[h])*scales[h])+x)*192/grid;
            float cy=(sigmoid((q[1]-zero[h])*scales[h])+y)*192/grid;
            float w=expf((q[2]-zero[h])*scales[h])*anchors[h][a][0];
            float ht=expf((q[3]-zero[h])*scales[h])*anchors[h][a][1];
            FaceBox box={cx-w*.5f,cy-ht*.5f,cx+w*.5f,cy+ht*.5f,score};
            out[count++]=box;
        }
    }
    return count;
}
// Startup/reference adapter uses the same shared NMS as the live graph.
// Model clipping is deliberately after NMS; common NMS has no image bounds.
uint32_t face_decode(const int8_t *head0,const int8_t *head1,FaceBox *work,FaceBox *out)
{
    uint32_t n=0,count=face_decode_candidates(head0,head1,work);
    if(scored_box_nms(work,count,.45f,work,out,FACE_MAX_CANDIDATES,&n)) return 0;
    for(unsigned i=0;i<n;++i) {
        out[i].x0=fminf(192,fmaxf(0,out[i].x0)); out[i].x1=fminf(192,fmaxf(0,out[i].x1));
        out[i].y0=fminf(192,fmaxf(0,out[i].y0)); out[i].y1=fminf(192,fmaxf(0,out[i].y1));
    }
    return n;
}

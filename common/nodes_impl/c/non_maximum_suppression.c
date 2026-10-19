// Copyright (c) 2022 Arm Limited. All rights reserved.
// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// IoU/NMS translated from MLEK DetectorPostProcessing; original attribution
// and Apache license are preserved in projects/object_detection/model/NOTICE.md.
#include "NonMaximumSuppression.hpp"
#include <math.h>
#include <string.h>
static float overlap(const ScoredBox *a,const ScoredBox *b)
{
    float w=fmaxf(0,fminf(a->x1,b->x1)-fmaxf(a->x0,b->x0));
    float h=fmaxf(0,fminf(a->y1,b->y1)-fmaxf(a->y0,b->y0));
    float inter=w*h,area=(a->x1-a->x0)*(a->y1-a->y0)+(b->x1-b->x0)*(b->y1-b->y0)-inter;
    return area>0 ? inter/area : 0;
}
int scored_box_nms(const ScoredBox *input,uint32_t count,float threshold,
                   ScoredBox *work,ScoredBox *output,uint32_t capacity,uint32_t *output_count)
{
    if(!output_count || !work || !output || (count && !input) || count>capacity ||
       !(threshold>=0 && threshold<=1)) return -1;
    *output_count=0;
    // Build a stable sorted copy without modifying the caller's candidate list.
    // In-place input/work is supported because insertion visits increasing indices.
    for(uint32_t i=0;i<count;++i) {
        ScoredBox box=input[i]; uint32_t pos=i;
        while(pos && work[pos-1].score<box.score) { work[pos]=work[pos-1]; --pos; }
        work[pos]=box;
    }
    for(uint32_t i=0;i<count;++i) {
        if(work[i].score==0) continue;
        ScoredBox best=work[i];
        for(uint32_t j=i+1;j<count;++j)
            if(work[j].score && overlap(&best,&work[j])>threshold) work[j].score=0;
        output[(*output_count)++]=best;
    }
    return 0;
}

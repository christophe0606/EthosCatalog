// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "rnnoise_dsp.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
int main(int argc,char** argv) {
    if(argc!=6) return 1;
    FILE *in=fopen(argv[1],"rb"), *features=fopen(argv[2],"wb"), *audio=fopen(argv[3],"wb");
    if(!in||!features||!audio) return 2;
    void *analysis=rn_dsp_create(), *synthesis=rn_dsp_create();
    if(!analysis||!synthesis) return 3;
    int16_t pcm[480]; float f[42],context[RN_CONTEXT],result[RN_RESULT],gains[23];
    for(unsigned i=0;i<22;++i) gains[i]=float(atof(argv[4]));
    gains[22]=0.75f;
    const unsigned reset=unsigned(atoi(argv[5])); unsigned frame=0;
    while(fread(pcm,2,480,in)==480) {
        if(frame==reset) {rn_dsp_reset(analysis);rn_dsp_reset(synthesis);}
        rn_analyze(analysis,pcm,f,context,frame>=reset);
        rn_synthesize(synthesis,context,gains,result);
        for(float v:result) if(!std::isfinite(v)) return 4;
        if(result[481]!=0.75f || std::abs(result[482]-gains[0])>1.e-6f) return 5;
        fwrite(f,4,42,features); fwrite(result,4,480,audio); ++frame;
    }
    fclose(in);fclose(features);fclose(audio);
    rn_dsp_destroy(analysis);rn_dsp_destroy(synthesis);return 0;
}

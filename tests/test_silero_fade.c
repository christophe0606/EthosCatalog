// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "../projects/silero_vad/nodes_impl/vad_fade.h"
// Same pure timing function used by the LCD node; execute on host or target.
int silero_fade_test(void) {
 if(vad_fade_alpha(100,100,1000,1)!=255)return -430;
 if(vad_fade_alpha(600,100,1000,1)!=128)return -431;
 if(vad_fade_alpha(1099,100,1000,1)!=1)return -432;
 if(vad_fade_alpha(1100,100,1000,1)!=0)return -433;
 if(vad_fade_alpha(5000,100,1000,1)!=0)return -434;
 // New detection at700 replaces the previous100 timestamp mid-fade.
 if(vad_fade_alpha(700,700,1000,1)!=255 || vad_fade_alpha(1200,700,1000,1)!=128 || vad_fade_alpha(1700,700,1000,1)!=0)return -435;
 if(vad_fade_alpha(0x20,0xfffffff0U,1000,1)!=243)return -436;
 if(vad_fade_alpha(0,0,1000,0)!=0 || vad_fade_alpha(0,0,0,1)!=0)return -437;
 return 0;
}

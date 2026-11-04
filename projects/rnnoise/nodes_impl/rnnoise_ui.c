// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>
// 5x7 column bitmaps: 130 bytes, scaled while drawing. No text-image atlas.
static const uint8_t letters[26][5] = {
 {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
 {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
 {0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
 {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
 {0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},
 {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
 {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
 {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},
 {0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
 {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},
 {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
 {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
 {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
};
static void text(uint16_t *fb,const char *s,unsigned y,unsigned scale,uint16_t color) {
    unsigned x=(800-strlen(s)*6*scale)/2;
    for (;*s;++s,x+=6*scale) {
        static const uint8_t digits[10][5]={{62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30}};
        const uint8_t* glyph=(*s>='A'&&*s<='Z')?letters[*s-'A']:(*s>='0'&&*s<='9')?digits[*s-'0']:0;
        if(!glyph){if(*s=='.')for(unsigned dx=0;dx<scale;++dx)for(unsigned dy=0;dy<scale;++dy)fb[(x+2*scale+dx)*480+479-(y+6*scale+dy)]=color;if(*s=='-')for(unsigned dx=0;dx<4*scale;++dx)for(unsigned dy=0;dy<scale;++dy)fb[(x+dx)*480+479-(y+3*scale+dy)]=color;continue;}
        for(unsigned c=0;c<5;++c) for(unsigned r=0;r<7;++r)
            if(glyph[c]&(1U<<r))
                for(unsigned dx=0;dx<scale;++dx) for(unsigned dy=0;dy<scale;++dy)
                    fb[(x+c*scale+dx)*480+479-(y+r*scale+dy)]=color;
    }
}
void rnnoise_render(uint16_t* fb,float p,float db,float gain,float ms,float frame,uint32_t count,uint32_t gaps){
 memset(fb,0,800*480*2);char line[80];text(fb,"RNNOISE MICROPHONE",24,4,0xF81F);
 snprintf(line,sizeof(line),"VOICE PROBABILITY %.1F PERCENT",100*p);text(fb,line,100,3,0xFFFF);
 snprintf(line,sizeof(line),"INPUT LEVEL %.1F DBFS",db);text(fb,line,158,3,0xFFFF);
 snprintf(line,sizeof(line),"MASK SUPPRESSION %.1F DB",gain);text(fb,line,216,3,0xFFFF);
 snprintf(line,sizeof(line),"INFER MAX %.2F MS  FRAME %.2F MS",ms,frame);text(fb,line,274,3,0x7FE0);
 snprintf(line,sizeof(line),"FRAMES %lu  CAPTURE GAPS %lu",(unsigned long)count,(unsigned long)gaps);text(fb,line,332,3,0xFFFF);
 text(fb,"ENHANCED AUDIO DISCARDED",414,3,0x7BEF);
}

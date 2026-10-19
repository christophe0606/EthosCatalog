// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "FaceDisplay.hpp"
#include <stdint.h>
// Landscape canvas: 800x480 logical pixels, centered 384x384 model preview.
// The panel scanout is 480x800: one clockwise transform maps the entire
// landscape canvas into its physical framebuffer. Image and boxes share it.
enum { LANDSCAPE_WIDTH=800, LANDSCAPE_HEIGHT=480, PANEL_WIDTH=480,
       MODEL_SIZE=192, SCALE=2,
       ORIGIN_X=(LANDSCAPE_WIDTH-MODEL_SIZE*SCALE)/2,
       ORIGIN_Y=(LANDSCAPE_HEIGHT-MODEL_SIZE*SCALE)/2 };
static void landscape_pixel(uint16_t *lcd,unsigned x,unsigned y,uint16_t color)
{
    lcd[x*PANEL_WIDTH+(LANDSCAPE_HEIGHT-1-y)]=color;
}
static void pixel(uint16_t *lcd,unsigned x,unsigned y,uint16_t color)
{
    unsigned dx=ORIGIN_X+SCALE*x,dy=ORIGIN_Y+SCALE*y;
    for(unsigned yy=0;yy<SCALE;++yy) for(unsigned xx=0;xx<SCALE;++xx)
        landscape_pixel(lcd,dx+xx,dy+yy,color);
}
// Convert continuous model edges to pixel indices only after NMS. Box edges
// may be outside the crop or equal 192; valid preview indices end at 191.
static unsigned edge_pixel(float edge) { return edge<=0 ? 0 : edge>=191 ? 191 : (unsigned)edge; }
void face_render(uint16_t *lcd,const uint8_t *rgb,const FaceBox *boxes,uint32_t count)
{
    for(unsigned y=0;y<192;++y) for(unsigned x=0;x<192;++x) {
        const uint8_t *p=rgb+3*(y*192+x);
        pixel(lcd,x,y,(uint16_t)(((p[0]>>3)<<11)|((p[1]>>2)<<5)|(p[2]>>3)));
    }
    for(unsigned i=0;i<count;++i) {
        unsigned x0=edge_pixel(boxes[i].x0),y0=edge_pixel(boxes[i].y0);
        unsigned x1=edge_pixel(boxes[i].x1),y1=edge_pixel(boxes[i].y1);
        for(unsigned x=x0;x<=x1;++x) { pixel(lcd,x,y0,0xf800); pixel(lcd,x,y1,0xf800); }
        for(unsigned y=y0;y<=y1;++y) { pixel(lcd,x0,y,0xf800); pixel(lcd,x1,y,0xf800); }
    }
}

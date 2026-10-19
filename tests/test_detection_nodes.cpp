// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Execute production wrappers using Stream FIFOs and exclusive mock DMA/LCD.
#include "CameraCapture.hpp"
#include "Rgb565CenterCrop.hpp"
#include "Rgb888ToGrayInt8.hpp"
#include "NonMaximumSuppression.hpp"
#include "FaceInference.hpp"
#include "FaceDecode.hpp"
#include "FaceDisplay.hpp"
#include <cassert>
#include <cstdio>
#include <memory_resource>
#include <vector>
std::pmr::synchronized_pool_resource pool;
uint32_t test_tick;
extern "C" { volatile uint32_t camera_frames,camera_errors,camera_timeouts; }
static unsigned captures,presents;
static int capture_error,inference_error,display_error;
static unsigned present_ms=50;
static void *expected_module=(void*)0x1234;
static uint16_t *previous_buffer;
extern "C" int camera_capture(uint16_t *p,uint32_t)
{
    assert(captures==presents); // DMA cannot overwrite an unpresented frame.
    if(capture_error) return capture_error;
    ++captures; ++camera_frames; test_tick+=20;
    std::fill_n(p,BOARD_CAMERA_WIDTH*BOARD_CAMERA_HEIGHT,uint16_t(0x07e0));
    return 0;
}
extern "C" int detection_forward(void *module,const int8_t *input,int8_t *h0,int8_t *h1,uint32_t *ticks)
{
    assert(module==expected_module);
    assert(input[0]==21); // Expanded pure 565 green -> truncated 149 - 128.
    if(inference_error) return inference_error;
    std::fill_n(h0,648,int8_t(-128)); std::fill_n(h1,2592,int8_t(-128));
    *ticks=3000; test_tick+=3;
    return 0;
}
extern "C" int lcd_present(uint16_t *p)
{
    assert(captures==presents+1);
    assert(p!=previous_buffer); // Released buffer reused only on the next frame.
    if(display_error) return display_error;
    previous_buffer=p; ++presents; test_tick+=present_ms;
    return 0;
}
static void reusable_nodes()
{
    // Rectangular crops, padded rows and independent capacities: downstream
    // views must reference owned storage rather than the camera/DMA input.
    for(auto dims : {std::array<unsigned,6>{9,7,12,5,3,15}, {8,6,10,4,4,32}}) {
        unsigned w=dims[0],h=dims[1],stride=dims[2],cw=dims[3],ch=dims[4];
        std::vector<uint16_t> pixels(stride*h,0x07e0);
        Rgb565Image a[1]{{pixels.data(),w,h,stride,42,123}}; Rgb888Image b[1]; GrayInt8Image c[1];
        FIFO<Rgb565Image,1,1,0> fa(a); FIFO<Rgb888Image,1,1,0> fb(b); FIFO<GrayInt8Image,1,1,0> fc(c);
        Rgb565CenterCrop<Rgb565Image,1,Rgb888Image,1> crop(fa,fb,{int(cw),int(ch)});
        Rgb888ToGrayInt8<Rgb888Image,1,GrayInt8Image,1> gray(fb,fc,{int(dims[5])});
        assert(crop.init()==0 && gray.init()==0 && crop.run()==0);
        std::fill(pixels.begin(),pixels.end(),0xf800);
        assert(b[0].width==cw && b[0].height==ch && b[0].stride==cw*3);
        assert(b[0].pixels[0]==0 && b[0].pixels[1]==255 && b[0].pixels[2]==0);
        assert(gray.run()==0 && c[0].sequence==42 && c[0].started==123 && c[0].stride==cw);
        assert(c[0].pixels!=reinterpret_cast<const int8_t*>(b[0].pixels) && c[0].pixels[cw*ch-1]==21);
        a[0].width=cw-1; assert(crop.run()!=0);
        b[0].width=dims[5]+1; b[0].height=1; assert(gray.run()!=0);
    }
    for(float threshold : {.2f,.8f}) {
        ScoredBox input[]={{0,0,10,10,.6f},{5,0,15,10,.9f},{-20,-20,-10,-10,.9f},{0,0,0,0,.7f}};
        ScoredBox original[4]; std::copy_n(input,4,original);
        ScoredBoxList a[1]{{input,4,7,99}},b[1];
        FIFO<ScoredBoxList,1,1,0> fa(a),fb(b);
        NonMaximumSuppression<ScoredBoxList,1,ScoredBoxList,1> nms(fa,fb,{8,threshold});
        assert(nms.init()==0 && nms.run()==0 && b[0].count==(threshold<.5f?3u:4u));
        assert(b[0].boxes[0].x0==5 && b[0].boxes[1].x0==-20 && b[0].boxes[2].score==.7f);
        assert(b[0].sequence==7 && b[0].started==99 && !std::memcmp(input,original,sizeof(input)));
        input[1].x0=777; assert(b[0].boxes[0].x0==5);
        a[0].count=9; assert(nms.run()!=0);
        a[0].count=0; a[0].boxes=nullptr; assert(nms.run()==0 && b[0].count==0);
    }
}
int main()
{
    reusable_nodes();
    Rgb565Image a[1]; Rgb888Image b[1],rgb1[1],rgb2[1]; GrayInt8Image g[1];
    InferredFrame c[1]; ScoredBoxList d[1],e[1]; PreparedFrame f[1];
    FIFO<Rgb565Image,1,1,0> fa(a); FIFO<Rgb888Image,1,1,0> fb(b),fr1(rgb1),fr2(rgb2);
    FIFO<GrayInt8Image,1,1,0> fg(g); FIFO<InferredFrame,1,1,0> fc(c);
    FIFO<ScoredBoxList,1,1,0> fd(d),fe(e); FIFO<PreparedFrame,1,1,0> ff(f);
    static uint16_t raw[640*480],lcd0[480*800],lcd1[480*800];
    static int8_t input[192*192]; StreamStats stats{};
    HardwareParams hw{expected_module,input,raw,{lcd0,lcd1},&stats};
    CameraCapture<Rgb565Image,1> source(fa,{hw});
    Rgb565CenterCrop<Rgb565Image,1,Rgb888Image,1> crop(fa,fb,{192,192});
    Duplicate<Rgb888Image,1,Rgb888Image,1> duplicate(fb,{&fr1,&fr2});
    Rgb888ToGrayInt8<Rgb888Image,1,GrayInt8Image,1> gray(fr1,fg,{192*192});
    FaceInference<GrayInt8Image,1,Rgb888Image,1,InferredFrame,1> network(fg,fr2,fc,{hw});
    FaceDecode<InferredFrame,1,ScoredBoxList,1,PreparedFrame,1> decoder(fc,fd,ff,{hw});
    NonMaximumSuppression<ScoredBoxList,1,ScoredBoxList,1> nms(fd,fe,{540,.45f});
    FaceDisplay<ScoredBoxList,1,PreparedFrame,1> display(fe,ff,{hw,200});
    assert(crop.init()==0 && gray.init()==0 && network.init()==0 && nms.init()==0);
    auto preprocess=[&] { assert(crop.run()==0 && duplicate.run()==0 && gray.run()==0); };
    auto postprocess=[&] { assert(decoder.run()==0 && nms.run()==0); };
    for(unsigned i=0;i<100;++i) {
        assert(source.run()==0); preprocess(); assert(network.run()==0); postprocess(); assert(display.run()==0);
        assert(f[0].rgb==b[0].pixels && f[0].input==g[0].pixels);
    }
    assert(test_tick==20000 && stats.captured==100 && stats.frames_presented==100);
    assert(stats.cropped==100 && stats.grayscaled==100 && stats.preprocessed==100 && stats.inferences==100 && stats.decoded==100 && stats.nms==100);
    assert(stats.frame_deadline_misses==0 && stats.frame_ticks_max==73000);
    capture_error=-99; assert(source.run()==-99); assert(stats.captured==100); capture_error=0;
    assert(source.run()==0);
    assert(source.run()==-820 && stats.ownership_errors==1); // Deliberate double capture rejected.
    preprocess(); inference_error=-98; assert(network.run()==-98); inference_error=0;
    assert(network.run()==0); postprocess();
    display_error=-97; assert(display.run()==-97 && stats.frames_presented==100); display_error=0;
    present_ms=250; assert(display.run()==0 && stats.frame_deadline_misses==1);
    assert(display.run()==-821 && stats.ownership_errors==2); // Stale publication rejected.
    std::puts("PASS: reusable node geometries/thresholds, 100 production frames, branch lifetimes, DMA/LCD ownership and deadlines");
}

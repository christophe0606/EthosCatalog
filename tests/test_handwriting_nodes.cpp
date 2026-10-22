// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "SentenceInput.hpp"
#include "SendToNetwork.hpp"
#include "HandwritingDisplay.hpp"
#include <cassert>
#include <deque>
#include <fstream>
#include <memory_resource>
#include <limits>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
std::pmr::synchronized_pool_resource pool;
void* EventQueue::handlerData=nullptr;
EventQueue::AppHandler EventQueue::handler=nullptr;
std::atomic<bool> EventQueue::handlerReady_=false;
template<> std::array<uint16_t,2> SendToNetwork<SentenceSnapshot,1>::selectors{100,101};
std::array<uint16_t,1> HandwritingDisplay::selectors{102};
uint16_t HandwritingDisplay::frames_[2][480*800];
namespace handwriting { const char alphabet[]="\0 !\"#'(),-.0123456789:;?ABCDEFGHIJKLMNOPRSTUVWYabcdefghijklmnopqrstuvwxyz"; }
class Queue:public EventQueue {
public:
    std::deque<Message> messages;
    bool push(Message&& m) final { messages.push_back(std::move(m));return true; }
    bool isEmpty() final { return messages.empty(); }
    void clear() final { messages.clear(); }
    void execute() final {}
};
class Sink:public StreamNode {};
static uint16_t* latest;
static int acquire_error;
static uint16_t* held[2]; static uint64_t hashes[2]; static unsigned held_count;
static uint64_t hash_frame(const uint16_t* p) { uint64_t h=1;for(unsigned i=0;i<480*800;++i) h=h*33+p[i];return h; }
static uint16_t cached[2][480*800]; static uint16_t* buffers[2];
static int present(uint16_t* p,unsigned x,unsigned y,unsigned width,unsigned height) {
    assert(x<=480 && y<=800 && width<=480-x && height<=800-y);
    unsigned slot=p==buffers[0]?0:1;buffers[slot]=p;
    // Every modified pixel must be included in the declared clean rectangle.
    for(unsigned row=0;row<800;++row) for(unsigned col=0;col<480;++col) {
        unsigned i=row*480+col;
        if(p[i]!=cached[slot][i]) assert(col>=x && col<x+width && row>=y && row<y+height);
    }
    unsigned first=(x*2U)&~31U,last=((x+width)*2U+31U)&~31U;
    assert(first<=last && last<=960); // rounded spans never cross a native row
    std::memcpy(cached[slot],p,sizeof(cached[slot]));
    held_count=0;if(latest) { held[held_count]=latest;hashes[held_count++]=hash_frame(latest); }
    held[held_count]=p;hashes[held_count++]=hash_frame(p);latest=p;return 0;
}
static int display_init(uint16_t* p) { latest=p;buffers[0]=p;std::memcpy(cached[0],p,sizeof(cached[0]));return 0; }
static int acquire() {
    if(acquire_error) return acquire_error;
    // Both old and newly submitted buffers remain immutable until release.
    for(unsigned i=0;i<held_count;++i) assert(hash_frame(held[i])==hashes[i]);
    held_count=0;return 0;
}
int main(int argc,char** argv) {
#if defined(_WIN32)
    // Compare the actual UART bytes, without Windows text-mode LF conversion.
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    StreamStats stats{}; HardwareParams hw{}; hw.stats=&stats;hw.lcd_init=display_init;hw.lcd_acquire=acquire;hw.lcd_present=present;
    UartBlock block{};SentenceSnapshot snapshot{};
    FIFO<UartBlock,1,1,0> bytes(&block); FIFO<SentenceSnapshot,1,1,0> text(&snapshot);
    SentenceInput<UartBlock,1,SentenceSnapshot,1> editor(bytes,text,{hw});
    auto input=[&](const std::string& s) {
        for(size_t i=0;i<s.size();i+=32) {
            block.count=unsigned(std::min(size_t(32),s.size()-i));std::memcpy(block.bytes,s.data()+i,block.count);
            assert(editor.run()==CG_SUCCESS);
        }
    };
    input("ab\bcd\r\n");assert(stats.sentences==1 && std::string(snapshot.text)=="acd");
    assert(snapshot.bias==1.0f && snapshot.seed==42 && snapshot.seed_revision==0);
    input("QXZ\r");assert(stats.sentences==2 && std::string(snapshot.text)=="qxz" && stats.normalized==3);
    input(std::string(76,'a')+"\r");assert(stats.rejected==1 && stats.sentences==2);
    input("bad@\r");assert(stats.rejected==2 && stats.sentences==2);
    input("Hi@\b there\r");assert(stats.sentences==3 && std::string(snapshot.text)=="Hi there");
    input("a@b\b\bc\r");assert(stats.sentences==4 && std::string(snapshot.text)=="ac");
    input(std::string(76,'a')+"\b\r");assert(stats.sentences==5 && std::strlen(snapshot.text)==75);
    input("unfinished\x15Hello\r");assert(std::string(snapshot.text)=="Hello");
    const auto frozen=snapshot;
    input(":bias .75\r\n:seed 7\r:help\r");
    assert(snapshot.generation==frozen.generation && snapshot.bias==1 && snapshot.seed==42 && snapshot.seed_revision==0);
    assert(handwriting::current(frozen));
    const auto rejected=stats.rejected;
    for(const char* bad:{":bias nan\r",":bias inf\r",":bias 2.1\r",":bias -1\r",
                         ":bias 1x\r",":seed 0\r",":seed 4294967296\r",":seed -2\r"}) input(bad);
    assert(stats.rejected==rejected+8 && snapshot.generation==frozen.generation);
    input("Hello\r");assert(snapshot.bias==.75f && snapshot.seed==7 && snapshot.seed_revision==1 && !handwriting::current(frozen));
    input("Hello\r");assert(snapshot.seed==7 && snapshot.seed_revision==1);
    input(":seed\r:bias .75\rHello\r");assert(snapshot.seed_revision==1);
    input(":seed 7\rHello\r");assert(snapshot.seed==7 && snapshot.seed_revision==2);
    input(":bias 2\r:seed 4294967295\r::hello\r");
    assert(std::string(snapshot.text)==":hello" && snapshot.bias==2 && snapshot.seed==UINT32_MAX && snapshot.seed_revision==3);
    input(":bias 1\r:seed 42\rHello\r");
    Queue q;Sink sink;SendToNetwork<SentenceSnapshot,1> send(text,&q,{hw});send.subscribe(0,sink,0);
    assert(send.run()==0 && q.messages.size()==1);
    auto request=*reinterpret_cast<const SentenceSnapshot*>(uintptr_t(q.messages.front().event.get<uint64_t>()));
    for(int i=0;i<100;++i) assert(send.run()==0);
    assert(q.messages.size()==1); // repeated dataflow firings cannot flood inference
    input("Replacement\r");assert(!handwriting::current(request));
    Event ack(101,kNormalPriority,uint32_t(request.generation),uint32_t(0));
    assert(send.processEvent(0,std::move(ack))==0);q.clear();
    assert(send.run()==0 && q.messages.size()==1);
    HandwritingDisplay display({hw});assert(display.init()==0);
    handwriting::Result r{snapshot,{},1,0,false};
    static uint16_t reference[480*800];handwriting::Layout reference_layout;
    auto deliver=[&] {
        if(handwriting::current(r.request)) {
            if(r.kind==1) { std::memset(reference,0,sizeof(reference));reference_layout.reset(); }
            if(r.kind==0) {
                auto s=reference_layout.add(r.point.dx,r.point.dy,r.point.pen_up,r.character,r.word_boundary,r.punctuation);
                if(s.ink && !s.full) handwriting::draw_ink(s.fx0,s.fy0,s.fx1,s.fy1,[&](int x,int y,uint16_t color) {
                    if(x>=0 && x<800 && y>=0 && y<480) {
                        auto &old=reference[handwriting::Layout::native_index(x,y)];old=std::max(old,color);
                    }
                });
            }
        }
        Event e(102,kNormalPriority,uint64_t(reinterpret_cast<uintptr_t>(&r)));assert(display.processEvent(0,std::move(e))==0);
        assert(std::memcmp(latest,reference,sizeof(reference))==0); // partial vs full-copy reference
    };
    deliver();assert(stats.clears==1);
    r.kind=0;r.point={1,0,0};deliver();r.point={3,2,0};deliver();assert(stats.drawn==2);
    unsigned white=0,gray=0;for(unsigned i=0;i<480*800;++i) {
        white+=latest[i]==65535;gray+=latest[i]!=0 && latest[i]!=65535;
    }
    assert(white && gray);
    auto before=stats.frames_presented;input("New\r");deliver();assert(stats.frames_presented==before && stats.stale_results==1);
    // Run real saved quantized trajectories through the actual production layout.
    // Input triples + attention character estimate are generated by Python below.
    assert(argc==2);std::ifstream f(argv[1]);float dx,dy,up;unsigned ch,word;
    handwriting::Layout layout;unsigned wrapped=0,points=0;
    r.request=snapshot;r.kind=1;deliver();r.kind=0;
    while(f>>dx>>dy>>up>>ch>>word) {
        auto s=layout.add(dx,dy,up,ch,word!=0);assert(!s.full);wrapped+=s.wrapped;++points;
        r.point={dx,dy,up};r.character=ch;r.word_boundary=word!=0;deliver();
    }
    assert(points>1000 && wrapped>=1 && layout.lines()>=2);
    assert(stats.ink_overflows==0);
    assert(handwriting::Layout::native_index(0,0)==479 && handwriting::Layout::native_index(799,479)==383520);
    handwriting::Layout bounded;unsigned full=0;
    for(unsigned i=0;i<1000;++i) full+=bounded.add(5,0,1,i,true).full;
    assert(full && bounded.lines()==3);
    // Trailing punctuation can use the reserved right-hand space instead of
    // becoming a detached new line. The hard viewport bound still applies.
    handwriting::Layout punctuation,letters;
    assert(handwriting::punctuation_suffix("Hi ...",2));
    assert(handwriting::punctuation_suffix("Hi ...",6));
    assert(!handwriting::punctuation_suffix("Hi there.",2));
    assert(!handwriting::punctuation_suffix("Hi \"there\"",2));
    punctuation.add(220,0,1,0,false);letters.add(220,0,1,0,false);
    assert(!punctuation.add(10,0,1,1,false,true).wrapped);
    assert(!punctuation.add(4,0,1,2,false,true).wrapped);
    assert(letters.add(10,0,1,1,false).wrapped);
    assert(punctuation.add(20,0,1,3,false,true).wrapped);
    std::ofstream image(std::string(argv[1])+".ppm",std::ios::binary);image<<"P6\n800 480\n255\n";
    for(unsigned y=0;y<480;++y) for(unsigned x=0;x<800;++x) {
        const auto color=latest[handwriting::Layout::native_index(x,y)];
        image.put(char(((color>>11)&31)*255/31));image.put(char(((color>>5)&63)*255/63));image.put(char((color&31)*255/31));
    }
    assert(stats.lcd_copy_bytes<uint64_t(points)*768000U/100U);
    assert(stats.lcd_clean_bytes<uint64_t(points)*768000U/100U);
    std::cout<<"Partial copies="<<stats.lcd_copy_bytes<<" cache-clean bytes="<<stats.lcd_clean_bytes<<"\n";
    // IEEE-bit guards remain effective with the production fast-math policy.
    const float huge=std::numeric_limits<float>::max();
    handwriting::Layout safe;
    safe.add(1,0,0,0,false);
    for(const auto delta:{huge,-huge,std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        auto a=safe.add(delta,0,0,0,false),b=safe.add(0,delta,0,0,false);
        assert(a.rejected && b.rejected && !a.ink && !b.ink && safe.lines()==1);
    }
    auto recovered=safe.add(1,0,0,0,false);
    assert(!recovered.rejected && !recovered.ink && recovered.x1==36 && recovered.y1==90);
    assert(safe.add(1,0,0,0,false).ink);
    // Repeated individually reasonable deltas cannot accumulate unbounded state.
    for(int axis=0;axis<3;++axis) {
        handwriting::Layout accumulated;unsigned rejected=0;
        for(unsigned i=0;i<10000;++i) {
            auto s=accumulated.add(axis==0?-100.0f:0.0f,axis==1?-100.0f:axis==2?100.0f:0.0f,0,0,false);
            if(s.rejected) ++rejected;
            else assert(s.x0>=-4096 && s.x0<=4096 && s.x1>=-4096 && s.x1<=4096 &&
                        s.y0>=-4096 && s.y0<=4096 && s.y1>=-4096 && s.y1<=4096);
        }
        assert(rejected>9900);
        auto s=accumulated.add(axis==0?1.0f:0.0f,axis==1?1.0f:axis==2?-1.0f:0.0f,0,0,false);
        assert(!s.rejected && !s.ink);
    }
    unsigned raster_calls=0;auto count=[&](int,int){++raster_calls;};
    // Wrapping resets local origins, but must not permit retained absolute
    // coordinates to grow beyond their separate bound.
    handwriting::Layout absolute;
    for(unsigned i=0;i<5;++i) assert(!absolute.add(0,-250,0,0,false).rejected);
    absolute.add(240,0,0,0,false);assert(absolute.add(10,0,0,0,false).wrapped);
    for(unsigned i=0;i<5;++i) assert(!absolute.add(0,-250,0,0,false).rejected);
    absolute.add(230,0,0,0,false);assert(absolute.add(10,0,0,0,false).wrapped);
    assert(absolute.lines()==3 && absolute.add(0,-250,0,0,false).rejected);
    auto absolute_recovery=absolute.add(0,1,0,0,false);
    assert(!absolute_recovery.rejected && !absolute_recovery.ink && absolute_recovery.y1==367);
    const int imin=std::numeric_limits<int>::min(),imax=std::numeric_limits<int>::max();
    assert(!handwriting::draw_line(imin,imin,imax,imax,count));
    assert(!handwriting::draw_line(imin,0,imin+1,0,count));
    assert(!handwriting::draw_line(imax-1,0,imax,0,count));
    assert(!handwriting::draw_line(0,imin,0,imin+1,count));
    assert(!handwriting::draw_line(0,imax-1,0,imax,count));
    assert(!handwriting::draw_line(imax,imax,imax,imax,count));
    assert(!handwriting::draw_line(0,0,1601,0,count) && raster_calls==0);
    assert(handwriting::draw_line(4095,4095,4096,4096,count) && raster_calls>0);
    // Fractional geometry, symmetric round caps, and invalid inputs. Drawing
    // a segment twice uses max coverage, so joints cannot build up bright blobs.
    uint16_t pen[16][16]{};auto ink=[&](int x,int y,uint16_t color) {
        assert(x>=0 && x<16 && y>=0 && y<16);pen[y][x]=std::max(pen[y][x],color);
    };
    assert(handwriting::draw_ink(4,8,11,8,ink));
    assert(pen[8][4]==65535 && pen[7][7]>0 && pen[7][7]==pen[9][7]);
    assert(pen[8][3]>0 && pen[8][3]==pen[8][12] && !pen[8][2]);
    uint16_t pen_copy[16][16];std::memcpy(pen_copy,pen,sizeof(pen));
    assert(handwriting::draw_ink(11,8,4,8,ink) && !std::memcmp(pen_copy,pen,sizeof(pen)));
    std::memset(pen,0,sizeof(pen));assert(handwriting::draw_ink(4.5f,8.5f,10.5f,8.5f,ink));
    assert(pen[8][7]==pen[9][7] && pen[8][7]!=pen_copy[8][7]);
    unsigned ink_calls=0;auto invalid_ink=[&](int,int,uint16_t){++ink_calls;};
    assert(!handwriting::draw_ink(huge,0,0,0,invalid_ink));
    assert(!handwriting::draw_ink(0,std::numeric_limits<float>::quiet_NaN(),0,0,invalid_ink));
    assert(!handwriting::draw_ink(0,0,1601,0,invalid_ink) && !ink_calls);
    // The production display reports a rejected model delta without modifying
    // pixels; a valid continuation after the outlier remains usable.
    auto overflow_before=stats.ink_overflows,frames_before=stats.frames_presented;
    r.point={huge,0,0};r.kind=0;deliver();
    assert(stats.ink_overflows==overflow_before+1 && stats.frames_presented==frames_before);
    r.point={1,0,0};deliver();assert(stats.ink_overflows==overflow_before+1);
    acquire_error=-123;r.kind=1;
    Event failure(102,kNormalPriority,uint64_t(reinterpret_cast<uintptr_t>(&r)));
    assert(display.processEvent(0,std::move(failure))==-123 && stats.error==-123);
    std::cout<<"PASS: editor, sampling settings, CRLF, bounds, normalization, one-inflight, replacement, antialiased ink, rotation and "<<points<<" real trajectory points on "<<layout.lines()<<" lines\n";
}

// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#if RVTTS_TEST_FARGAN
#include "FarganVocoder.hpp"
using Vocoder=FarganVocoder;
#else
#include "LpcVocoder.hpp"
using Vocoder=LpcVocoder;
#endif
#include "PlaybackSource.hpp"
#include <cassert>
#include <deque>
#include <iostream>
#include <thread>
#include <vector>
std::pmr::synchronized_pool_resource pool;
unsigned test_tick=0,frame_calls=0;
void *EventQueue::handlerData=nullptr;
EventQueue::AppHandler EventQueue::handler=nullptr;
std::atomic<bool> EventQueue::handlerReady_{false};
std::array<uint16_t,2> Vocoder::selectors{100,101};
using Playback=PlaybackSource<uint32_t,1,AudioBlock,1>;
template<> std::array<uint16_t,2> Playback::selectors{100,102};

// Predictable stateful frame engines exercise the real production nodes and
// crop/rounding/flush logic independently of model accuracy self-tests.
static float sample(unsigned frame,unsigned feature,unsigned i){
    return float(int((frame*173+feature*19+i*37)%60001)-30000)/32768.f;
}
#if RVTTS_TEST_FARGAN
extern "C" void rvtts_fargan_cpu_prime(RvttsFarganCpuState*,const float*){frame_calls=0;}
extern "C" void rvtts_fargan_cpu_frame(RvttsFarganCpuState*,const float *f,float *pcm){
    for(unsigned i=0;i<160;++i)pcm[i]=sample(frame_calls,unsigned(f[0]),i);
    ++frame_calls;
}
#else
extern "C" void lpcvoc_init(LpcVocState*){frame_calls=0;}
extern "C" void lpcvoc_frame(LpcVocState*,const float *f,int16_t *pcm){
    for(unsigned i=0;i<160;++i)pcm[i]=int16_t(std::floor(sample(frame_calls,unsigned(f[0]),i)*32768.f+.5f));
    ++frame_calls;
}
#endif
class Queue:public EventQueue {
public:
    std::deque<Message> messages;bool reject=false;
    bool push(Message&&m) final {if(reject)return false;messages.push_back(std::move(m));return true;}
    bool isEmpty() final{return messages.empty();}
    void clear() final{messages.clear();}
    void execute() final{}
    void deliver(){
        assert(!messages.empty());auto m=std::move(messages.front());messages.pop_front();
        auto dst=std::get<LocalDestination>(m.destination);
        assert(dst.dst->processEvent(dst.dstPort,std::move(m.event))==CG_SUCCESS);
    }
};
class Ack:public StreamNode {
public:
    unsigned count=0,generation=0;
    cg_status processEvent(int,Event&&e) final{++count;generation=e.get<uint32_t>();return CG_SUCCESS;}
};
static void utterances(unsigned event_burst){
    Queue events;Ack ack;StreamStats stats{};HardwareParams hw{};hw.stats=&stats;
    uint32_t dummy=0;AudioBlock out{};
    FIFO<uint32_t,1,1,0> input(&dummy);FIFO<AudioBlock,1,1,0> output(&out);
    Playback playback(input,output,&events,PlaybackSourceParams{hw});
#if RVTTS_TEST_FARGAN
    Vocoder vocoder(&events,FarganVocoderParams{hw});
#else
    Vocoder vocoder(&events,LpcVocoderParams{hw});
#endif
    vocoder.subscribe(0,playback,0);vocoder.subscribe(1,vocoder,1);playback.subscribe(0,ack,0);
    rvtts::Work work{};
    unsigned generation=0;
    for(unsigned frames:{1u,2u,19u,92u,224u,1u}){
        work.frames=frames;work.generation=++generation;work.started=osKernelGetSysTimerCount();
        for(unsigned t=0;t<frames;++t)work.features[t*20]=float(t);
        std::vector<int16_t> expected,actual;
        const unsigned calls=frames+(RVTTS_TEST_FARGAN?2:0);
        for(unsigned t=0;t<calls;++t){
            unsigned first=RVTTS_TEST_FARGAN&&!t?80:0;
            unsigned last=RVTTS_TEST_FARGAN&&t==frames+1?80:160;
            for(unsigned i=first;i<last;++i)
                expected.push_back(int16_t(std::floor(sample(t,std::min(t,frames-1),i)*32768.f+.5f)));
        }
        assert(vocoder.processEvent(0,Event(100,kNormalPriority,rvtts::address(&work)))==CG_SUCCESS);
        bool overlapped=false;unsigned turns=0;
        while(ack.count<generation){
            assert(++turns<20000);
            for(unsigned i=0;i<event_burst&&!events.isEmpty();++i)events.deliver();
            assert(playback.run()==CG_SUCCESS);
            if(out.generation){
                assert(out.generation==generation);
                assert(out.position*160==actual.size());
                assert(out.total*160==expected.size());
                if(actual.empty())overlapped=frame_calls<calls;
                actual.insert(actual.end(),out.samples,out.samples+160);
            }
            ++test_tick;
        }
        while(!events.isEmpty())events.deliver();
        assert(actual==expected);
        assert(stats.pcm_hash==stats.played_hash);
        assert(stats.pcm_hash==rvtts::pcmHash(2166136261u,expected.data(),unsigned(expected.size())));
        assert(stats.pcm_queue_max<=rvtts::PcmQueue::Capacity);
        assert(stats.playback_starvations==0);
        if(frames>=19)assert(overlapped);
        assert(ack.generation==generation);
        assert(playback.run()==CG_SUCCESS&&out.generation==0);
    }
    if(event_burst>1)assert(stats.pcm_queue_waits>0 || stats.generated_blocks<=4);
}
static void threaded_queue(){
    rvtts::PcmQueue queue;queue.reset();
    constexpr unsigned blocks=20000;
    std::thread producer([&]{
        int16_t block[160];
        for(unsigned b=0;b<blocks;++b){
            for(unsigned i=0;i<160;++i)block[i]=int16_t(b*173+i);
            while(!queue.push(block))std::this_thread::yield();
        }
    });
    int16_t block[160];
    for(unsigned b=0;b<blocks;++b){
        while(!queue.pop(block))std::this_thread::yield();
        for(unsigned i=0;i<160;++i)assert(block[i]==int16_t(b*173+i));
    }
    producer.join();assert(queue.available()==0&&queue.highWater()<=4);
}
int main(){
    static_assert(sizeof(rvtts::PcmQueue)<2048,"PCM storage must stay independent of sentence length");
    utterances(1);utterances(12);threaded_queue();
    std::cout<<"Streaming node checks PASS: ordered PCM, crop/flush, overlapping generation, bounded backpressure, repeated utterances, threaded queue\n";
}

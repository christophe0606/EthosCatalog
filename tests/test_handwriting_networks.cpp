// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Exercise the production model wrappers/controller with deterministic dense
// outputs. ExecuTorch is replaced at its single dense-call boundary only.
#define ETHOSU_ARCH
#define APP_READ_CLOCK_HZ 100000000
#include "HandwritingNetwork.hpp"
#include "HandwritingSynthesis.hpp"
#include <cassert>
#include <cmath>
#include <deque>
#include <memory_resource>
#include <limits>
#include <vector>

std::pmr::synchronized_pool_resource pool;
void* EventQueue::handlerData=nullptr;
EventQueue::AppHandler EventQueue::handler=nullptr;
std::atomic<bool> EventQueue::handlerReady_=false;
std::array<uint16_t,1> HandwritingNetwork::selectors{103};
std::array<uint16_t,4> HandwritingSynthesis::selectors{100,101,102,103};
volatile ethos_benchmark_t ethos_benchmark{};
void ethos_benchmark_reset() {
    ethos_benchmark.cycles=0; ethos_benchmark.active_cycles=0;
    ethos_benchmark.invocations=0; ethos_benchmark.overflow=0;
}

static std::vector<unsigned> calls;
static int failing_method=-1;
static bool complete=false;
static bool attend_end=false;
namespace handwriting {
int Engine::dense_view(unsigned method,const float* input,const float*& view,uint32_t*) {
    // Every method reuses and overwrites this storage, like a shared arena.
    // The production stage must consume its view before invoking another method.
    static float output[1600];
    view=nullptr;
    calls.push_back(method);
    if(int(method)==failing_method) return -421;
    const auto& s=state();
    auto same=[](const float* a,const float* b,unsigned n) {
        for(unsigned i=0;i<n;++i) assert(a[i]==b[i]);
    };
    // Check packing against the current recurrent state at every boundary,
    // including the old window for lstm1/attention and new window for lstm2/3.
    if(method<=1) {
        same(input,s.window,73); same(input+76,s.h[0],400);
        assert(input[73]==s.previous.dx && input[74]==s.previous.dy && input[75]==s.previous.pen_up);
    } else if(method<=3) {
        same(input+3,s.h[method-2],400); same(input+403,s.window,73); same(input+476,s.h[method-1],400);
        assert(input[0]==s.previous.dx && input[1]==s.previous.dy && input[2]==s.previous.pen_up);
    } else same(input,s.h[2],400);
    std::fill(output,output+output_sizes[method],.01f*(method+1));
    if(method==1) {
        std::fill(output,output+30,-10.0f);
        if(complete || attend_end) std::fill(output+20,output+30,25.0f*(s.length-1));
    }
    if(method==4) output[120]=complete?100.0f:-100.0f;
    view=output;
    ++ethos_benchmark.invocations; ethos_benchmark.cycles+=100; ethos_benchmark.active_cycles+=100;
    return 0;
}
}

class Queue:public EventQueue {
public:
    std::deque<Message> messages;
    bool reject=false;
    bool push(Message&& message) final {
        if(reject) return false;
        messages.push_back(std::move(message)); return true;
    }
    bool isEmpty() final { return messages.empty(); }
    void clear() final { messages.clear(); }
    void execute() final {}
    cg_status dispatch() {
        assert(!messages.empty());
        auto message=std::move(messages.front()); messages.pop_front();
        auto destination=std::get<LocalDestination>(message.destination);
        return destination.dst->processEvent(destination.dstPort,std::move(message.event));
    }
    void drain() { while(!isEmpty()) assert(dispatch()==CG_SUCCESS); }
};
class Results:public StreamNode {
public:
    std::vector<unsigned> order;
    handwriting::Result last{};
    uint32_t acknowledged=0,done=0;
    cg_status processEvent(int port,Event&& event) final {
        if(port==0) {
            assert(event.event_id==102);
            last=*reinterpret_cast<const handwriting::Result*>(uintptr_t(event.get<uint64_t>()));
            order.push_back(last.kind);
        } else {
            assert(event.event_id==101);
            event.apply<uint32_t,uint32_t>([&](uint32_t generation,uint32_t finished) {
                acknowledged=generation; done=finished;
            });
            order.push_back(10);
        }
        return CG_SUCCESS;
    }
};

int main() {
    StreamStats stats{}; HardwareParams hw{}; hw.stats=&stats;
    Queue queue; Results results;
    HandwritingSynthesis synthesis(&queue,{hw});
    HandwritingNetwork lstm1(&queue,{hw,0}),attention(&queue,{hw,1}),
        lstm2(&queue,{hw,2}),lstm3(&queue,{hw,3}),gmm(&queue,{hw,4});
    HandwritingNetwork* models[]={&lstm1,&attention,&lstm2,&lstm3,&gmm};
    synthesis.subscribe(0,results,1); synthesis.subscribe(1,results,0); synthesis.subscribe(2,lstm1,0);
    for(unsigned i=0;i<5;++i) {
        assert(models[i]->init()==CG_SUCCESS);
        if(i<4) models[i]->subscribe(0,*models[i+1],0); else models[i]->subscribe(0,synthesis,1);
    }
    HandwritingNetwork invalid(&queue,{hw,5}); assert(invalid.init()==CG_BUFFER_ERROR);
    std::atomic<uint32_t> generation{1};
    SentenceSnapshot request{}; request.generation=1; request.current=&generation;
    request.seed=42;request.bias=1;
    std::strcpy(request.text,"A");
    auto start=[&] {
        calls.clear(); results.order.clear();
        Event event(100,kNormalPriority,uint64_t(reinterpret_cast<uintptr_t>(&request)));
        return synthesis.processEvent(0,std::move(event));
    };
    assert(start()==CG_SUCCESS && calls.empty() && results.order==std::vector<unsigned>{1});
    // A new request cannot overwrite Work while any model still holds it.
    Event duplicate(100,kNormalPriority,uint64_t(reinterpret_cast<uintptr_t>(&request)));
    assert(synthesis.processEvent(0,std::move(duplicate))==CG_BUFFER_ERROR);
    for(unsigned method=0;method<5;++method) {
        assert(queue.messages.size()==1 && queue.dispatch()==CG_SUCCESS);
        assert(calls.size()==method+1 && calls.back()==method);
    }
    queue.drain();
    assert(results.order==std::vector<unsigned>({1,0,10}));
    assert(stats.inferences==1 && stats.generated==1 && !results.done);
    // Recurrent state persists across strokes; model input checks above run
    // on every call. The 40*length limit runs no models and still finishes/ACKs.
    for(unsigned i=1;i<40;++i) { assert(start()==0); queue.drain(); assert(calls.size()==5); }
    assert(start()==0); queue.drain();
    assert(calls.empty() && stats.step_limits==1 && results.done==1);
    assert(results.order==std::vector<unsigned>({3,10}));
    // Replacement between two network nodes suppresses the old point and
    // releases the request. The replacement resets the recurrence and clears.
    request.generation=2; generation=2;
    assert(start()==0); assert(queue.dispatch()==0); generation=3;
    queue.drain(); assert(stats.stale_results==1 && results.order==std::vector<unsigned>({1,10}));
    request.generation=3; assert(start()==0); queue.drain();
    assert(calls==std::vector<unsigned>({0,1,2,3,4}) && results.order==std::vector<unsigned>({1,0,10}));
    // Already stale requests ACK without clearing or invoking any model.
    request.generation=2; assert(start()==0); queue.drain();
    assert(calls.empty() && results.order==std::vector<unsigned>{10});
    // EOS sampling is performed by gmm and reports completion before ACK.
    request.generation=4; generation=4; complete=true;
    assert(start()==0); queue.drain();
    assert(stats.completed==1 && results.done && results.order==std::vector<unsigned>({1,2,10}));
    // NUL attention can precede the final pen lift. Retain the final character's
    // punctuation policy so its finishing strokes stay on the preceding line.
    complete=false;attend_end=true;
    request.generation=5;generation=5;std::strcpy(request.text,"Hi...");
    assert(start()==0);queue.drain();
    assert(results.last.kind==0 && results.last.character==5 && results.last.punctuation);
    attend_end=false;
    // A failed method stops the chain before subsequent methods/results/ACK.
    request.generation=6; generation=6; complete=false; failing_method=2;
    assert(start()==0);
    assert(queue.dispatch()==0 && queue.dispatch()==0 && queue.dispatch()==-421);
    assert(queue.isEmpty() && calls==std::vector<unsigned>({0,1,2}));
    assert(results.order==std::vector<unsigned>{1});
    // Order guards reject a skipped/repeated model before dense execution.
    handwriting::Engine engine(nullptr); handwriting::Stroke point{};
    calls.clear(); assert(engine.begin("A")==0);
    assert(engine.stage(1,point)==-435 && calls.empty());
    assert(engine.stage(0,point)==0 && engine.stage(0,point)==-435 && calls.size()==1);
    // Queue-full errors are surfaced at the responsible model node.
    handwriting::Work work{&engine,{},0,0}; queue.reject=true;
    Event advance(103,kNormalPriority,uint64_t(reinterpret_cast<uintptr_t>(&work)));
    assert(attention.processEvent(0,std::move(advance))==CG_BUFFER_ERROR && stats.event_drops==1);
    // A failed stage may be retried without losing the retained packed prefix.
    // A replacement sentence must rebuild that prefix at stage 0.
    for(unsigned failure=0;failure<5;++failure) {
        failing_method=-1; calls.clear();
        assert(engine.begin("AB")==0);
        for(unsigned method=0;method<5;++method) {
            if(method==failure) {
                failing_method=int(method);
                const auto before=engine.state();
                assert(engine.stage(method,point)==-421);
                assert(std::memcmp(&before,&engine.state(),sizeof(before))==0);
                failing_method=-1;
            }
            assert(engine.stage(method,point)==0);
        }
        assert(engine.state().steps==1 && engine.finite());
    }
    // Bias scales the Gaussian sampling noise without changing dense layers or
    // recurrent state. The old default remains reproducible for benchmarks.
    handwriting::Stroke baseline{},repeat{},legible{};
    assert(engine.begin("AB",42,.5f)==0 && engine.step(baseline)==0);
    assert(engine.begin("AB",42)==0 && engine.step(repeat)==0);
    assert(std::memcmp(&baseline,&repeat,sizeof(baseline))==0);
    assert(engine.begin("AB",42,1)==0 && engine.step(legible)==0);
    assert(std::fabs((legible.dx-.05f)-(baseline.dx-.05f)*std::exp(-.5f))<.00001f);
    assert(std::fabs((legible.dy-.05f)-(baseline.dy-.05f)*std::exp(-.5f))<.00001f);
    const auto saved_state=engine.state();
    for(float bias:{-1.0f,2.1f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        assert(engine.begin("AB",42,bias)==-424);
        assert(std::memcmp(&saved_state,&engine.state(),sizeof(saved_state))==0);
    }
    std::cout<<"PASS: five model nodes, recurrence, sampling bias, stage order, backpressure, replacement, EOS, step bound and errors\n";
}

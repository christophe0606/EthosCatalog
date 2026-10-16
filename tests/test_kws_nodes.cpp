// SPDX-License-Identifier: Apache-2.0
#include "KeywordLCD.hpp"
#include "KwsEvents.hpp"
#include <cassert>
#include <deque>
#include <memory_resource>
#include <cstdio>
std::pmr::synchronized_pool_resource pool;
uint32_t test_tick;
void *EventQueue::handlerData=nullptr;
EventQueue::AppHandler EventQueue::handler=nullptr;
std::atomic<bool> EventQueue::handlerReady_=false;
std::array<uint16_t,2> KeywordLCD::selectors{101,100};
template<> std::array<uint16_t,1> KwsEvents<float,10>::selectors{100};
class Queue : public EventQueue {
public:
    std::deque<Message> messages;
    bool reject=false;
    bool push(Message&& m) final { if(reject) return false; messages.push_back(std::move(m)); return true; }
    bool isEmpty() final { return messages.empty(); }
    void clear() final { messages.clear(); }
    void execute() final {}
    cg_status deliver() {
        auto m=std::move(messages.front()); messages.pop_front();
        auto d=std::get<LocalDestination>(m.destination);
        return d.dst->processEvent(d.dstPort,std::move(m.event));
    }
};
static uint16_t *presented;
static int present(uint16_t *fb) { presented=fb; return 0; }
static void tick(KeywordLCD& lcd) {
    Event event(100,kNormalPriority,uint32_t(test_tick));
    assert(lcd.processEvent(0,std::move(event))==CG_SUCCESS);
}
static unsigned word_brightness() {
    unsigned result=0;
    for(unsigned x=0;x<800;++x) for(unsigned y=205;y<261;++y)
        result+=presented[x*480+479-y];
    return result;
}
int main() {
    static uint16_t frame[2][800*480+2];
    frame[0][0]=frame[1][0]=frame[0][800*480+1]=frame[1][800*480+1]=0xdead;
    StreamStats stats{}; HardwareParams hw{};
    hw.stats=&stats; hw.framebuffers[0]=frame[0]+1; hw.framebuffers[1]=frame[1]+1; hw.lcd_present=present;
    KeywordLCD lcd(KeywordLCDParams{hw,1000,20});
    assert(lcd.init()==CG_SUCCESS);
    tick(lcd); assert(word_brightness()==0);
    test_tick=100;
    Event recognition(101,kNormalPriority,uint32_t(0),uint32_t(test_tick));
    assert(lcd.processEvent(0,std::move(recognition))==CG_SUCCESS);
    tick(lcd); unsigned full=word_brightness(); assert(full>0 && stats.display_alpha==255);
    test_tick=600; tick(lcd); assert(word_brightness()<full && stats.display_alpha>=127 && stats.display_alpha<=128);
    test_tick=1100; tick(lcd); assert(word_brightness()==0 && stats.display_alpha==0);
    // A new command restarts the fade; all labels fit the guarded framebuffer.
    for(unsigned word=0;word<10;++word) {
        Event event(101,kNormalPriority,uint32_t(word),uint32_t(test_tick));
        assert(lcd.processEvent(0,std::move(event))==CG_SUCCESS);
        tick(lcd); assert(word_brightness()>0 && stats.displayed_class==word);
    }
    for(unsigned i=0;i<2;++i) assert(frame[i][0]==0xdead && frame[i][800*480+1]==0xdead);
    // Rendering happens only when the event queue is delivered, never in run().
    Queue queue;
    float values[10]{}; FIFO<float,10,1,0> fifo(values);
    KwsEvents<float,10> clock(fifo,&queue,KwsEventsParams{hw,20});
    assert(clock.init()==CG_SUCCESS); clock.subscribe(0,lcd,0);
    unsigned before=stats.frames_presented;
    test_tick=2000; clock.run();
    assert(stats.frames_presented==before && queue.messages.size()==1);
    assert(queue.deliver()==CG_SUCCESS && stats.frames_presented==before+1);
    queue.reject=true; test_tick+=60; clock.run(); assert(stats.lcd_events_dropped==1);
    Event malformed(101,kNormalPriority,int32_t(1));
    assert(lcd.processEvent(0,std::move(malformed))==CG_BUFFER_ERROR);
    std::puts("KWS nodes: PASS (queued LCD, one-second fade, new words, frame bounds, queue rejection)");
}

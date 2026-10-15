// SPDX-License-Identifier: Apache-2.0
#include "MicrophoneSource.hpp"
#include "PCMToFloat.hpp"
#include "SlidingBuffer.hpp"
#include "RemoveDC.hpp"
#include "HannWindow.hpp"
#include "SendToLCD.hpp"
#include "LCD.hpp"
#include <cassert>
#include <cmath>
#include <deque>
#include <memory_resource>
#include <iostream>
std::pmr::synchronized_pool_resource pool;
uint32_t test_tick;
void *EventQueue::handlerData = nullptr;
EventQueue::AppHandler EventQueue::handler = nullptr;
std::atomic<bool> EventQueue::handlerReady_ = false;
template<> std::array<uint16_t,1> SendToLCD<SpectrumBlock,1>::selectors{100};
std::array<uint16_t,1> LCD::selectors{100};
class Queue : public EventQueue {
public:
    std::deque<Message> messages;
    bool reject = false;
    bool push(Message&& m) final { if (reject) return false; messages.push_back(std::move(m)); return true; }
    bool isEmpty() final { return messages.empty(); }
    void clear() final { messages.clear(); }
    void execute() final {}
    cg_status deliver() {
        auto m=std::move(messages.front()); messages.pop_front();
        auto dst=std::get<LocalDestination>(m.destination);
        return dst.dst->processEvent(dst.dstPort,std::move(m.event));
    }
};
static bool gap;
static int starts, presents, renders, touches, releases;
static int mic_start() { ++starts; return 0; }
static int mic_read(int16_t* pcm, bool* discontinuity) {
    for (unsigned i=0;i<MIC_BLOCK_SAMPLES;++i) pcm[i]=i & 1 ? 16384 : -16384;
    *discontinuity=gap; return 0;
}
static int touch(unsigned* x,unsigned* y,bool* contact) { ++touches; *x=10; *y=10; *contact=false; return 0; }
static int present(uint16_t*) { ++presents; return 0; }
static bool ui_touch(unsigned,unsigned,bool) { return false; }
static void release() { ++releases; }
static void render(uint16_t*,bool) { ++renders; }
int main() {
    StreamStats stats{};
    HardwareParams hw{};
    hw.stats=&stats; hw.microphone_start=mic_start; hw.microphone_read=mic_read;
    PcmBlock pcm_data{}; AudioBlock audio_data{}; FFTBlock history{}, dc_data{}, window_data{};
    FIFO<PcmBlock,1,1,0> pcm(&pcm_data); FIFO<AudioBlock,1,1,0> audio(&audio_data);
    FIFO<FFTBlock,1,1,0> sliding_fifo(&history), dc_fifo(&dc_data), window_fifo(&window_data);
    MicrophoneSource<PcmBlock,1> mic(pcm,MicrophoneSourceParams{hw});
    PCMToFloat<PcmBlock,1,AudioBlock,1> convert(pcm,audio);
    SlidingBuffer<AudioBlock,1,FFTBlock,1> sliding(audio,sliding_fifo);
    RemoveDC<FFTBlock,1,FFTBlock,1> dc(sliding_fifo,dc_fifo);
    HannWindow<FFTBlock,1,FFTBlock,1> window(dc_fifo,window_fifo);
    assert(mic.init()==CG_SUCCESS && sliding.init()==CG_SUCCESS && window.init()==CG_SUCCESS);
    for (unsigned i=0;i<6;++i) {
        ++test_tick; mic.run(); convert.run();
        assert(audio_data.samples[0]==-0.5f && audio_data.samples[1]==0.5f);
        sliding.run(); dc.run(); window.run();
        assert(history.info.valid==(i>=3));
        if (i>=3) {
            assert(history.samples[0]==-0.5f && dc_data.samples[1]==0.5f);
            assert(std::fabs(window_data.samples[0])<1e-6f);
            assert(std::fabs(window_data.info.amplitude_scale-4.0f/SPECTRUM_FFT_SIZE)<1e-6f);
        }
    }
    assert(starts==1);
    for (unsigned i=0;i<4;++i) {
        gap=(i==0); mic.run(); convert.run(); sliding.run();
        assert(history.info.valid==(i==3));
    }
    assert(stats.discontinuities==1);

    // Hold three queued payloads while producing further spectra. Pending
    // events must remain immutable and capture must continue without waiting.
    SpectrumBlock spectrum{}; spectrum.info.valid=1;
    FIFO<SpectrumBlock,1,1,0> spectrum_fifo(&spectrum);
    Queue queue;
    SendToLCD<SpectrumBlock,1> send(spectrum_fifo,&queue,SendToLCDParams{hw,48000,512,20});
    float magnitude[SPECTRUM_BINS]{}; uint32_t peak=0; uint16_t frames[2]{};
    hw.lcd_magnitude=magnitude; hw.lcd_peak_hz=&peak;
    hw.framebuffers[0]=frames; hw.framebuffers[1]=frames+1;
    hw.lcd_touch=touch; hw.lcd_present=present; hw.ui_touch=ui_touch; hw.ui_release=release; hw.ui_render=render;
    LCD lcd(LCDParams{hw,2048,48000,20});
    assert(send.init()==CG_SUCCESS && lcd.init()==CG_SUCCESS);
    send.subscribe(0,lcd,0);
    for (unsigned i=0;i<4;++i) {
        test_tick+=50; spectrum.info.started=test_tick*1000;
        spectrum.samples[10]=float(i+1);
        send.run();
    }
    assert(queue.messages.size()==3 && stats.lcd_events_sent==3 && stats.lcd_events_dropped==1);
    for (unsigned i=0;i<3;++i) {
        assert(queue.deliver()==CG_SUCCESS);
        assert(magnitude[10]==float(i+1));
    }
    assert(presents==3 && renders==3 && touches==3 && peak==234);
    assert(stats.lcd_events_received==3 && stats.frames_presented==3);
    // An exhausted queue releases its event reference; a slot can be reused.
    queue.reject=true; test_tick+=50; send.run();
    assert(stats.lcd_events_dropped==2);
    queue.reject=false; test_tick+=50; spectrum.samples[10]=9; send.run();
    assert(queue.messages.size()==1 && queue.deliver()==CG_SUCCESS && magnitude[10]==9);
    spectrum.info.valid=0; test_tick+=50; send.run();
    assert(queue.isEmpty() && stats.warmup_blocks==1);
    Event malformed(100,kNormalPriority,int32_t(12));
    assert(lcd.processEvent(0,std::move(malformed))==CG_BUFFER_ERROR);
    std::cout << "Stream nodes: PASS (overlap, gap warmup, immutable queued spectra, bounded slots, queue rejection, LCD events)\n";
}

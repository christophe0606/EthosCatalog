// SPDX-License-Identifier: Apache-2.0
#include "MicrophoneSource.hpp"
#include "PCMToFloat.hpp"
#include "EthosGain.hpp"
#include "SlidingBuffer.hpp"
#include "FFT.hpp"
#include "RFFTMagnitude.hpp"
#include "RemoveDC.hpp"
#include "HannWindow.hpp"
#include "../projects/template_mram/nodes_impl/SendToLCD.hpp"
#include "../projects/template_mram/nodes_impl/LCD.hpp"
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
template<> std::array<uint16_t,1> SendToLCD<float,SPECTRUM_BINS>::selectors{100};
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
static unsigned captured, reads;
static bool ramp;
static int inference_error;
extern "C" int embedded_gain_forward(void*, float* in, float* out, size_t count) {
    if (inference_error) return inference_error;
    for (size_t i=0; i<count; ++i) out[i]=2.0f*in[i];
    return 0;
}
static int starts, presents, renders, touches, releases;
static int mic_start() { ++starts; return 0; }
static int mic_read(int16_t* pcm) {
    for (unsigned i=0;i<MIC_BLOCK_SAMPLES;++i,++captured)
        pcm[i] = ramp ? int16_t(captured) : int16_t(16384.0*std::sin(6.283185307179586*43*captured/SPECTRUM_FFT_SIZE));
    ++reads; return 0;
}
static int touch(unsigned* x,unsigned* y,bool* contact) { ++touches; *x=10; *y=10; *contact=false; return 0; }
static int present(uint16_t*) { ++presents; return 0; }
static bool ui_touch(unsigned,unsigned,bool) { return false; }
static void release() { ++releases; }
static float rendered_magnitude[SPECTRUM_BINS];
static void render(uint16_t*,const float* magnitudes,bool) {
    std::copy_n(magnitudes,SPECTRUM_BINS,rendered_magnitude);
    ++renders;
}
template<int N> static void test_fft_size() {
    float input[N], packed_storage[N+2]{}, bins_storage[N/2+3]{};
    float* packed=packed_storage+1;
    float* bins=bins_storage+1;
    packed_storage[0]=packed_storage[N+1]=12345.0f;
    bins_storage[0]=bins_storage[N/2+2]=12345.0f;
    std::fill_n(input,N,1.0f);
    FIFO<float,N,1,0> source(input), transformed(packed);
    FIFO<float,N/2+1,1,0> output(bins);
    FFT<float,N,float,N> fft(source,transformed);
    RFFTMagnitude<float,N,float,N/2+1> magnitude(transformed,output,RFFTMagnitudeParams{2.0f/N});
    assert(fft.init()==CG_SUCCESS && fft.run()==CG_SUCCESS && magnitude.run()==CG_SUCCESS);
    assert(std::fabs(bins[0]-1.0f)<1e-6f);
    for (int i=1;i<N/2+1;++i) assert(std::fabs(bins[i])<1e-6f);
    // Simultaneous DC, Nyquist and a complex interior bin distinguish the
    // special first pair from ordinary real/imaginary bin pairs.
    for (int i=0;i<N;++i) {
        const double angle=6.283185307179586*3*i/N;
        input[i]=float(-0.25 + ((i&1) ? 0.5 : -0.5)
                       + 0.75*std::cos(angle) + 0.25*std::sin(angle));
    }
    assert(fft.run()==CG_SUCCESS && magnitude.run()==CG_SUCCESS);
    assert(std::fabs(packed[0]/N+0.25f)<1e-6f); // Re(X[0])
    assert(std::fabs(packed[1]/N+0.5f)<1e-6f);  // Re(X[N/2]), not Im(X[0])
    assert(std::fabs(packed[6]/N-0.375f)<1e-6f);
    assert(std::fabs(packed[7]/N+0.125f)<1e-6f);
    for (int bin=0;bin<=N/2;++bin) {
        const float expected=bin==0 ? 0.25f : bin==N/2 ? 0.5f
                            : bin==3 ? std::sqrt(0.625f) : 0.0f;
        assert(std::fabs(bins[bin]-expected)<1e-6f);
    }
    assert(packed_storage[0]==12345.0f && packed_storage[N+1]==12345.0f);
    assert(bins_storage[0]==12345.0f && bins_storage[N/2+2]==12345.0f);
}
template<int Hop, int Frame> static void test_sliding_overlap() {
    // A downstream in-place stage may overwrite every output sample.
    // The next frame must still preserve its own overlap and startup padding.
    for (bool shared_storage : {false, true}) {
        float input[Hop]{}, output[Frame]{};
        float* source = shared_storage ? output : input;
        FIFO<float,Hop,1,0> in(source);
        FIFO<float,Frame,1,0> out(output);
        SlidingBuffer<float,Hop,float,Frame> node(in,out);
        for (unsigned reset=0; reset<2; ++reset) {
            assert(node.init()==CG_SUCCESS);
            for (int n=0; n<2*Frame; ++n) {
                for (int i=0; i<Hop; ++i) source[i]=float(n*Hop+i+1);
                assert(node.run()==CG_SUCCESS);
                for (int i=0; i<Frame; ++i) {
                    const int sample=(n+1)*Hop-Frame+i;
                    assert(output[i]==(sample<0 ? 0.0f : float(sample+1)));
                }
                std::fill_n(output,Frame,-999.0f);
            }
        }
    }
}
// Storage regressions: the production node must fit below the old 8 KiB
// history alone, and a zero-overlap node must remain small even for big frames.
static_assert(sizeof(SlidingBuffer<float,512,float,2048>) < 7*1024);
static_assert(sizeof(SlidingBuffer<float,2048,float,2048>) < 128);
int main() {
    test_sliding_overlap<1,8>(); test_sliding_overlap<3,8>();
    test_sliding_overlap<4,8>(); test_sliding_overlap<5,8>();
    test_sliding_overlap<8,8>(); test_sliding_overlap<1,1>();
    test_fft_size<32>(); test_fft_size<64>(); test_fft_size<128>(); test_fft_size<256>();
    test_fft_size<512>(); test_fft_size<1024>(); test_fft_size<2048>(); test_fft_size<4096>();
    StreamStats stats{};
    HardwareParams hw{};
    hw.stats=&stats; hw.microphone_start=mic_start; hw.microphone_read=mic_read;
    hw.microphone_block_samples=MIC_BLOCK_SAMPLES;
    // A source firing may end inside a hardware block or span two blocks.
    // Values must remain in order across both boundaries.
    {
        int16_t data[300]{};
        FIFO<int16_t,300,1,0> fifo(data);
        MicrophoneSource<int16_t,300> source(fifo,MicrophoneSourceParams{hw});
        assert(source.init()==CG_SUCCESS);
        ramp=true;
        for (unsigned n=0;n<6;++n) {
            assert(source.run()==CG_SUCCESS);
            for (unsigned i=0;i<300;++i) assert(data[i]==int16_t(n*300+i));
        }
        assert(reads==4 && starts==1);
    }
    ramp=false; captured=reads=starts=0; stats={};
    int16_t pcm_data[MIC_BLOCK_SAMPLES]{};
    float audio_data[MIC_BLOCK_SAMPLES]{}, gained[MIC_BLOCK_SAMPLES]{}, inference[MIC_BLOCK_SAMPLES]{};
    float history[SPECTRUM_FFT_SIZE]{}, dc_data[SPECTRUM_FFT_SIZE]{}, window_data[SPECTRUM_FFT_SIZE]{};
    float packed[SPECTRUM_FFT_SIZE]{}, bins[SPECTRUM_BINS]{};
    // Real FIFOs collect two 256-sample conversions for each 512-sample gain.
    FIFO<int16_t,MIC_BLOCK_SAMPLES> pcm(pcm_data);
    FIFO<float,MIC_BLOCK_SAMPLES> audio(audio_data);
    FIFO<float,MIC_BLOCK_SAMPLES,1,0> gain_fifo(gained);
    FIFO<float,SPECTRUM_FFT_SIZE,1,0> sliding_fifo(history), dc_fifo(dc_data), window_fifo(window_data), fft_fifo(packed);
    FIFO<float,SPECTRUM_BINS,1,0> bin_fifo(bins);
    hw.module=&hw; hw.inference_input=inference; hw.inference_samples=MIC_BLOCK_SAMPLES;
    MicrophoneSource<int16_t,MIC_BLOCK_SAMPLES> mic(pcm,MicrophoneSourceParams{hw});
    PCMToFloat<int16_t,256,float,256> convert(pcm,audio);
    EthosGain<float,MIC_BLOCK_SAMPLES,float,MIC_BLOCK_SAMPLES> gain(audio,gain_fifo,EthosGainParams{hw});
    SlidingBuffer<float,MIC_BLOCK_SAMPLES,float,SPECTRUM_FFT_SIZE> sliding(gain_fifo,sliding_fifo);
    RemoveDC<float,SPECTRUM_FFT_SIZE,float,SPECTRUM_FFT_SIZE> dc(sliding_fifo,dc_fifo);
    HannWindow<float,SPECTRUM_FFT_SIZE,float,SPECTRUM_FFT_SIZE> window(dc_fifo,window_fifo);
    FFT<float,SPECTRUM_FFT_SIZE,float,SPECTRUM_FFT_SIZE> fft(window_fifo,fft_fifo);
    RFFTMagnitude<float,SPECTRUM_FFT_SIZE,float,SPECTRUM_BINS> magnitude_node(fft_fifo,bin_fifo,RFFTMagnitudeParams{4.0f/SPECTRUM_FFT_SIZE});
    assert(mic.init()==CG_SUCCESS && gain.init()==CG_SUCCESS && sliding.init()==CG_SUCCESS && window.init()==CG_SUCCESS && fft.init()==CG_SUCCESS);
    for (unsigned n=0;n<6;++n) {
        ++test_tick;
        assert(mic.run()==CG_SUCCESS);
        for (unsigned c=0;c<2;++c) assert(convert.run()==CG_SUCCESS);
        assert(pcm.nbSamplesInFIFO()==0 && audio.nbSamplesInFIFO()==MIC_BLOCK_SAMPLES);
        for (unsigned i=0;i<MIC_BLOCK_SAMPLES;++i) assert(audio_data[i]==pcm_data[i]/32768.0f);
        assert(gain.run()==CG_SUCCESS && audio.nbSamplesInFIFO()==0);
        assert(sliding.run()==CG_SUCCESS);
        unsigned zeros=n<3 ? SPECTRUM_FFT_SIZE-(n+1)*MIC_BLOCK_SAMPLES : 0;
        for (unsigned i=0;i<zeros;++i) assert(history[i]==0);
        for (unsigned i=zeros;i<SPECTRUM_FFT_SIZE;++i) {
            unsigned sample=(n+1)*MIC_BLOCK_SAMPLES-(SPECTRUM_FFT_SIZE-i);
            float expected=int16_t(16384.0*std::sin(6.283185307179586*43*sample/SPECTRUM_FFT_SIZE))/16384.0f;
            assert(history[i]==expected);
        }
        assert(dc.run()==CG_SUCCESS && window.run()==CG_SUCCESS && fft.run()==CG_SUCCESS && magnitude_node.run()==CG_SUCCESS);
        if (n>=3) {
            unsigned peak=unsigned(std::max_element(bins+1,bins+SPECTRUM_BINS)-bins);
            assert(peak==43 && std::fabs(bins[43]-1.0f)<0.001f);
        }
    }
    assert(starts==1 && stats.audio_blocks==6);
    // Model length mismatches are rejected before copying to NPU memory.
    HardwareParams wrong=hw; wrong.inference_samples=256;
    EthosGain<float,MIC_BLOCK_SAMPLES,float,MIC_BLOCK_SAMPLES> bad_gain(audio,gain_fifo,EthosGainParams{wrong});
    assert(bad_gain.init()==CG_INIT_FAILURE);
    assert(mic.run()==CG_SUCCESS && convert.run()==CG_SUCCESS && convert.run()==CG_SUCCESS);
    inference_error=-123;
    assert(gain.run()==-123);
    inference_error=0;

    // Hold three queued payloads while producing further spectra. Pending
    // events must remain immutable and capture must continue without waiting.
    float spectrum[SPECTRUM_BINS]{};
    FIFO<float,SPECTRUM_BINS,1,0> spectrum_fifo(spectrum);
    Queue queue;
    SendToLCD<float,SPECTRUM_BINS> send(spectrum_fifo,&queue,SendToLCDParams{hw,48000,512,20});
    uint16_t frames[2]{};
    hw.framebuffers[0]=frames; hw.framebuffers[1]=frames+1;
    hw.lcd_touch=touch; hw.lcd_present=present; hw.ui_touch=ui_touch; hw.ui_release=release; hw.ui_render=render;
    LCD lcd(LCDParams{hw,2048,48000,20});
    assert(send.init()==CG_SUCCESS && lcd.init()==CG_SUCCESS);
    send.subscribe(0,lcd,0);
    for (unsigned i=0;i<4;++i) {
        test_tick+=50; stats.audio_started=test_tick*1000;
        spectrum[10]=float(i+1);
        send.run();
    }
    assert(queue.messages.size()==3 && stats.lcd_events_sent==3 && stats.lcd_events_dropped==1);
    for (unsigned i=0;i<3;++i) {
        assert(queue.deliver()==CG_SUCCESS);
        assert(rendered_magnitude[10]==float(i+1));
    }
    assert(presents==3 && renders==3 && touches==3 && lcd.peak_hz()==234);
    assert(stats.lcd_events_received==3 && stats.frames_presented==3);
    // An exhausted queue releases its event reference; a slot can be reused.
    queue.reject=true; test_tick+=50; send.run();
    assert(stats.lcd_events_dropped==2);
    queue.reject=false; test_tick+=50; spectrum[10]=9; send.run();
    assert(queue.messages.size()==1 && queue.deliver()==CG_SUCCESS && rendered_magnitude[10]==9);
    Event malformed(100,kNormalPriority,int32_t(12));
    assert(lcd.processEvent(0,std::move(malformed))==CG_BUFFER_ERROR);
    // Nodes sharing hardware still own independent spectrum snapshots and peaks.
    LCD other_lcd(LCDParams{hw,2048,48000,20});
    assert(other_lcd.init()==CG_SUCCESS && other_lcd.peak_hz()==0);
    float other_spectrum[SPECTRUM_BINS]{};
    other_spectrum[20]=7.0f;
    auto payload=TensorPtr<float>::create_with(SPECTRUM_BINS,
        UniquePtr<float>(other_spectrum,[](void*){}));
    Event other_event(100,kNormalPriority,payload);
    assert(other_lcd.processEvent(0,std::move(other_event))==CG_SUCCESS);
    assert(rendered_magnitude[10]==0 && rendered_magnitude[20]==7.0f);
    assert(other_lcd.peak_hz()==468 && lcd.peak_hz()==234);
    std::cout << "Stream nodes: PASS (sample reblocking, mixed rates, overlap-only storage, zero-padded frames, Ethos boundary, FFT amplitude, immutable spectra, bounded slots, LCD events)\n";
}

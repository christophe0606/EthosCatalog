// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "player/score_windows.hpp"
#include "player/instruments.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>
using namespace midi_ddsp;

static MidiEvent e(uint64_t time, MidiEventType type, uint8_t key=0,
                   uint32_t value=0, uint8_t channel=0) {
    return MidiEvent{0,time,0,value,type,channel,key};
}
static MidiEvent program(uint64_t time, unsigned value, uint8_t channel=0) {
    return e(time,MidiEventType::program,0,value,channel);
}
static void invariant(const ScoreWindow &w, unsigned limit) {
    assert(w.region_count && w.region_count<=limit && w.valid_frames<=1000);
    unsigned first=0;
    for(unsigned r=0;r<w.region_count;++r){
        const auto &region=w.regions[r];
        assert(region.first==first && region.length);
        for(unsigned f=first;f<first+region.length;++f){
            assert(w.frame_region[f]==r);
            assert(w.onset[f]==(f==first));
            assert(w.offset[f]==(f+1==first+region.length));
            float features[4];score_frame_features(w,f,features);
            assert(std::fabs(features[0]-float(region.pitch)/127)<1e-7f);
            assert(features[1]==w.onset[f] && features[2]==w.offset[f]);
            const float position=region.pitch?float(f-first+1)/region.length:0;
            assert(std::fabs(features[3]-position)<1e-7f);
        }
        first+=region.length;
    }
    assert(first==1000);
}
static ScoreWindow first_window(ScoreWindows &reader,const std::vector<MidiEvent>& events,
                               uint64_t duration,unsigned limit=64){
    assert(reader.configure(events.data(),events.size(),duration,limit));
    ScoreWindow w;assert(reader.next(w));invariant(w,limit);return w;
}
int main(int argc,char **argv){
    using T=MidiEventType;
    ScoreWindows reader;ScoreWindow w;
    assert(ScoreWindows::quantized_frame(1999)==0 && ScoreWindows::quantized_frame(2000)==1);
    assert(ScoreWindows::quantized_frame(5999)==1 && ScoreWindows::quantized_frame(6000)==2);
    const uint64_t max=std::numeric_limits<uint64_t>::max();
    assert(ScoreWindows::quantized_frame(max)==max/4000+(max%4000>=2000));
    w=first_window(reader,{program(0,40),e(0,T::note_on,60,100),e(500000,T::note_off,60),
        e(1000000,T::note_on,64,90),e(1500000,T::note_off,64)},2000000);
    assert(w.instrument==0 && w.valid_frames==500 && w.region_count==4);
    assert(w.regions[0].pitch==60 && w.regions[0].length==125);
    assert(w.regions[1].pitch==0 && w.regions[1].length==125);
    assert(w.regions[2].pitch==64 && w.regions[2].length==125);
    assert(w.regions[3].pitch==0 && w.regions[3].length==625);
    assert(w.controls[125].velocity==100 && w.controls[400].velocity==90);
    assert(!reader.next(w));
    // Adjacent same-key reattacks retain separate learned note regions.
    w=first_window(reader,{program(0,40),e(0,T::note_on,60,100),e(500000,T::note_off,60),
        e(500000,T::note_on,60,100),e(1000000,T::note_off,60)},4000000);
    assert(w.region_count==3 && w.regions[0].pitch==60 && w.regions[1].pitch==60);
    assert(w.regions[0].note_serial!=w.regions[1].note_serial && reader.counters().overlap_steals==0);
    // An old stolen key's release cannot release its newer same-key press.
    w=first_window(reader,{program(0,40),e(0,T::note_on,60,100),e(100000,T::note_on,60,80),
        e(200000,T::note_off,60),e(300000,T::note_off,60)},4000000);
    assert(w.region_count==3 && w.regions[0].length==25 && w.regions[1].length==50);
    assert(reader.counters().overlap_steals==1);
    // A different trained program is rejected for this window without stealing.
    std::vector<MidiEvent> events{program(0,40),e(0,T::note_on,60,100),program(100000,73),
        e(200000,T::note_on,76,100),e(300000,T::note_off,76),e(1000000,T::note_off,60),
        e(4000000,T::note_on,76,100),e(4500000,T::note_off,76)};
    w=first_window(reader,events,5000000);
    assert(w.instrument==0 && w.regions[0].length==250);
    assert(reader.counters().rejected_instrument_notes==1 && reader.counters().overlap_steals==0);
    assert(reader.next(w));invariant(w,64);
    assert(w.instrument==4 && w.instrument_changed && w.regions[0].pitch==76);
    // Boundary events are admitted in their new window, without an old lock.
    events={program(0,40),e(3000000,T::note_on,60,100),e(4000000,T::note_off,60),
        program(4000000,73),e(4000000,T::note_on,76,100),e(4500000,T::note_off,76)};
    w=first_window(reader,events,5000000);
    assert(w.regions[1].held_at_end);
    assert(reader.next(w));invariant(w,64);
    assert(w.instrument==4 && !w.regions[0].carried_in && reader.counters().rejected_instrument_notes==0);
    // Actual carry retains identity, but features are explicitly window-local.
    events={program(0,40),e(3000000,T::note_on,60,100),e(4500000,T::note_off,60)};
    w=first_window(reader,events,5000000);const uint32_t serial=w.regions[1].note_serial;
    assert(reader.next(w));invariant(w,64);
    assert(w.regions[0].carried_in && w.regions[0].note_serial==serial && w.regions[0].length==125);
    // Sustain, controllers and reset use source channel state independently.
    w=first_window(reader,{program(0,40),e(0,T::note_on,60,100),e(100000,T::control,64,127),
        e(200000,T::pitch_bend,0,9000),e(200000,T::control,7,50),e(200000,T::control,11,60),
        e(200000,T::control,10,20),e(500000,T::note_off,60),e(700000,T::control,121)},4000000);
    assert(w.regions[0].length==175 && w.controls[50].pitch_bend==9000 && w.controls[50].volume==50);
    assert(w.controls[50].expression==60 && w.controls[50].pan==20);
    assert(w.controls[175].pitch_bend==8192 && w.controls[175].expression==127);
    w=first_window(reader,{program(0,40),e(0,T::note_on,60,100),e(100000,T::control,64,127),
        e(500000,T::control,123),e(1000000,T::control,64,0)},4000000);
    assert(w.regions[0].length==250);
    w=first_window(reader,{program(0,40),e(0,T::note_on,60,100),e(100000,T::control,64,127),
        e(500000,T::control,120)},4000000);
    assert(w.regions[0].length==125);
    // Unsupported notes still consume FIFO press identities.
    w=first_window(reader,{program(0,40),e(0,T::note_on,60,100),program(10000,0),
        e(20000,T::note_on,60,100),e(30000,T::note_off,60),e(40000,T::note_off,60),
        program(50000,40),e(60000,T::note_on,0,100),program(70000,40,9),
        e(80000,T::note_on,60,100,9)},4000000);
    assert(w.regions[0].length==8 && reader.counters().unsupported_notes==3);
    assert(reader.counters().unmatched_note_offs==0);
    w=first_window(reader,{program(0,40),e(1,T::note_on,60,100),e(1000,T::note_off,60)},4000);
    assert(!w.has_notes && w.instrument==-1 && w.region_count==1 && reader.counters().quantized_away_notes==1);
    // Region work limit replaces a suffix with explicit hard-mute rest.
    events={program(0,40)};
    for(unsigned n=0;n<16;++n){events.push_back(e(n*8000,T::note_on,60,100));events.push_back(e(n*8000+4000,T::note_off,60));}
    w=first_window(reader,events,4000000,16);
    assert(w.desired_regions==32 && w.region_count==16 && w.capacity_rest_start==15);
    assert(reader.counters().dense_windows==1 && reader.counters().dense_note_regions==8);
    for(unsigned f=15;f<1000;++f)assert(w.regions[w.frame_region[f]].pitch==0 && w.controls[f].velocity==0);
    w=first_window(reader,{program(0,40),e(0,T::note_on,60,100),e(500000,T::note_off,60)},4000000,1);
    assert(w.region_count==1 && !w.has_notes && w.capacity_rest_start==0);
    // Every trained identity is independently admitted in aligned windows.
    events.clear();
    for(unsigned id=0;id<13;++id){const uint64_t t=uint64_t(id)*8000000;
        events.push_back(program(t,instruments[id].gm_program));events.push_back(e(t,T::note_on,60,100));events.push_back(e(t+1000000,T::note_off,60));}
    assert(reader.configure(events.data(),events.size(),104000000,16));
    unsigned windows=0;
    while(reader.next(w)){invariant(w,16);assert(w.instrument==int(windows/2));++windows;}
    assert(windows==26 && reader.counters().instrument_switches==12 && reader.counters().rejected_instrument_notes==0);
    assert(!reader.configure(nullptr,1,4000,16));
    assert(!reader.configure(nullptr,0,4000,0) && !reader.configure(nullptr,0,4000,65));
    events={e(2000,T::note_off,60),e(1000,T::note_off,60)};
    assert(!reader.configure(events.data(),events.size(),4000,16));
    events={e(5000,T::note_off,60)};assert(!reader.configure(events.data(),1,4000,16));
    assert(reader.configure(nullptr,0,0,16) && !reader.next(w));
    // Real SMF integration exercises parser tempo conversion and original MIDI.
    for(int i=1;i<argc;++i){
        std::ifstream input(argv[i],std::ios::binary);assert(input);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        std::vector<MidiEvent> parsed(4096);
        const auto score=parse_midi_file(bytes.data(),bytes.size(),parsed.data(),parsed.size());
        assert(score.error==MidiError::none && reader.configure(parsed.data(),score.event_count,score.duration_us,16));
        unsigned count=0;while(reader.next(w)){invariant(w,16);if(count==0)assert(w.instrument==0 && w.region_count==16);++count;}
        assert(count==(score.duration_us+3999999)/4000000);
        std::printf("Score fixture %s: %u windows, rejected streams %llu, dense windows %llu\n",argv[i],count,
            static_cast<unsigned long long>(reader.counters().rejected_instrument_notes),static_cast<unsigned long long>(reader.counters().dense_windows));
    }
    std::printf("Score windows PASS; state %zu bytes, output %zu bytes\n",sizeof(ScoreWindows),sizeof(ScoreWindow));
}

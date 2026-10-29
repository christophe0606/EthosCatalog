// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "EventLCD.hpp"
#include "SendToNetwork.hpp"
#include "YAMNet.hpp"
#include "YAMNetPostprocess.hpp"
#define YAMNET_WINDOW_BACKEND_ONLY
#include "AudioWindow.hpp"
#include <cassert>
#include <deque>
#include <iostream>
#include <memory_resource>
std::pmr::synchronized_pool_resource pool;
uint32_t test_tick;
void *EventQueue::handlerData = nullptr;
EventQueue::AppHandler EventQueue::handler = nullptr;
std::atomic<bool> EventQueue::handlerReady_ = false;
template <> std::array<uint16_t, 3> SendToNetwork<float, 64>::selectors{100, 101, 102};
volatile ethos_benchmark_t ethos_benchmark;
void ethos_benchmark_reset() {
    ethos_benchmark.cycles = ethos_benchmark.active_cycles = 0;
    ethos_benchmark.invocations = ethos_benchmark.overflow = 0;
}
int yamnet_forward(void *, float *input, float *scores, uint32_t *ticks) {
    assert(input);
    std::fill_n(scores, 521, 0.01f);
    for (unsigned i = 0; i < 5; ++i)
        scores[100 + i] = float(i + 1) / 10;
    *ticks = 123;
    ethos_benchmark.cycles = ethos_benchmark.active_cycles = 100;
    ethos_benchmark.invocations = 14;
    return 0;
}
static unsigned renders, presents;
void yamnet_print_health(const StreamStats *) {}
void yamnet_render(uint16_t *, const char *const *labels, const float *scores, const StreamStats *) {
    const char *expected[] = {"Roaring cats (lions, tigers)", "Wild animals", "Honk", "Goose", "Quack"};
    for (unsigned i = 0; i < 5; ++i) {
        assert(std::strcmp(labels[i], expected[i]) == 0);
        assert(scores[i] == float(5 - i) / 10);
    }
    ++renders;
}
int present(uint16_t *) {
    ++presents;
    return 0;
}
std::array<uint16_t, 3> YAMNet::selectors{100, 101, 103};
std::array<uint16_t, 3> YAMNetPostprocess::selectors{103, 102, 101};
std::array<uint16_t, 3> EventLCD::selectors{100, 101, 102};
class Queue : public EventQueue {
  public:
    std::deque<Message> messages;
    bool push(Message &&m) final {
        messages.push_back(std::move(m));
        return true;
    }
    bool isEmpty() final { return messages.empty(); }
    void clear() final { messages.clear(); }
    void execute() final {}
};
class Sink : public StreamNode {};
int main() {
    yamnet_window_t w{};
    float block[160], out[400];
    for (unsigned n = 0; n < 20; ++n) {
        for (unsigned i = 0; i < 160; ++i)
            block[i] = float(n * 160 + i);
        int valid = yamnet_window_push(&w, block, out);
        assert(valid == (n >= 2));
        if (valid)
            for (unsigned i = 0; i < 400; ++i)
                assert(out[i] == float((n - 2) * 160 + i));
    }
    float features[64]{};
    FIFO<float, 64, 1, 0> fifo(features);
    Queue q;
    Sink sink;
    StreamStats stats{};
    HardwareParams hw{};
    hw.stats = &stats;
    SendToNetwork<float, 64> send(fifo, &q, SendToNetworkParams{hw});
    send.subscribe(0, sink, 0);
    assert(send.init() == CG_SUCCESS);
    for (unsigned n = 0; n < 98; ++n) {
        std::fill_n(features, 64, float(n));
        assert(send.run() == CG_SUCCESS);
    }
    assert(q.messages.size() == 1 && stats.patches == 1 && stats.inference_requests == 1);
    auto &event = q.messages.front().event;
    assert(event.wellFormed<uint64_t>());
    auto *input = reinterpret_cast<float *>(uintptr_t(event.get<uint64_t>()));
    assert(input);
    assert(input[0] == 2 && input[6143] == 97);
    for (unsigned n = 98; n < 290; ++n) {
        std::fill_n(features, 64, float(n));
        assert(send.run() == CG_SUCCESS);
    }
    assert(q.messages.size() == 1 && stats.inference_requests == 1 && stats.coalesced_windows == 3);
    assert(input[0] == 2 && input[6143] == 97); // in-flight remains immutable
    q.clear();
    Event ack(101, kNormalPriority, uint32_t(1));
    assert(send.processEvent(0, std::move(ack)) == CG_SUCCESS);
    std::fill_n(features, 64, 290.0f);
    assert(send.run() == CG_SUCCESS);
    assert(q.messages.size() == 1 && stats.inference_requests == 2);
    auto *latest = reinterpret_cast<float *>(uintptr_t(q.messages.front().event.get<uint64_t>()));
    assert(latest[0] == 194 && latest[6143] == 289); // newest fixed-hop patch
    // Deliver a complete actual network/result/ack cycle through Stream events.
    q.clear();
    uint16_t fb[2];
    hw.module = &hw;
    hw.framebuffers[0] = fb;
    hw.framebuffers[1] = fb + 1;
    hw.lcd_present = present;
    YAMNet network(&q, YAMNetParams{hw});
    YAMNetPostprocess postprocess(&q);
    EventLCD lcd(EventLCDParams{hw});
    network.subscribe(0, send, 0);
    network.subscribe(1, postprocess, 0);
    postprocess.subscribe(0, lcd, 0);
    postprocess.subscribe(1, network, 1);
    assert(network.init() == CG_SUCCESS && lcd.init() == CG_SUCCESS);
    Event request(100, kNormalPriority, uint64_t(reinterpret_cast<uintptr_t>(latest)));
    assert(network.processEvent(0, std::move(request)) == CG_SUCCESS);
    assert(q.messages.size() == 1); // inference publishes raw scores, not classified LCD rows
    Event duplicate(100, kNormalPriority, uint64_t(reinterpret_cast<uintptr_t>(latest)));
    assert(network.processEvent(0, std::move(duplicate)) == CG_BUFFER_ERROR);
    Event wrong_sequence(101, kNormalPriority, uint32_t(99));
    assert(network.processEvent(1, std::move(wrong_sequence)) == CG_BUFFER_ERROR);
    assert(send.run() == CG_SUCCESS && stats.inference_requests == 2);
    while (!q.isEmpty()) {
        auto m = std::move(q.messages.front());
        q.messages.pop_front();
        auto dst = std::get<LocalDestination>(m.destination);
        assert(dst.dst->processEvent(dst.dstPort, std::move(m.event)) == CG_SUCCESS);
    }
    assert(renders == 1 && presents == 1 && stats.inferences == 1 && stats.frames_presented == 1);

    // Invalid scores must fail on the CPU before publishing any display data or acknowledgment.
    float invalid_scores[521]{};
    auto reject_scores = [&]() {
        Event scores(103, kNormalPriority,
                     uint64_t(reinterpret_cast<uintptr_t>(invalid_scores)), uint32_t(2));
        assert(postprocess.processEvent(0, std::move(scores)) == CG_BUFFER_ERROR);
        assert(q.isEmpty());
    };
    invalid_scores[520] = -0.1f;
    reject_scores();
    invalid_scores[520] = 1.1f;
    reject_scores();
    uint32_t nan_bits = 0x7fc00000U;
    std::memcpy(invalid_scores + 520, &nan_bits, sizeof(nan_bits));
    reject_scores();
    uint32_t infinity_bits = 0x7f800000U;
    std::memcpy(invalid_scores + 520, &infinity_bits, sizeof(infinity_bits));
    reject_scores();

    // Equal independent scores retain class-index order and are not normalized with softmax.
    std::fill_n(invalid_scores, 521, 0.25f);
    Event ties(103, kNormalPriority, uint64_t(reinterpret_cast<uintptr_t>(invalid_scores)), uint32_t(2));
    assert(postprocess.processEvent(0, std::move(ties)) == CG_SUCCESS);
    const char *tie_labels[] = {"Speech", "Child speech, kid speaking", "Conversation",
                                "Narration, monologue", "Babbling"};
    assert(q.messages.size() == 6);
    for (unsigned i = 0; i < 5; ++i) {
        q.messages.front().event.apply<uint32_t, uint64_t, float>(
            [&](uint32_t row, uint64_t address, float value) {
                assert(row == i && value == 0.25f);
                const char *label = reinterpret_cast<const char *>(uintptr_t(address));
                assert(std::strcmp(label, tie_labels[i]) == 0);
            });
        q.messages.pop_front();
    }
    q.clear();
    std::cout
        << "YAMNet alignment, bounded requests, CPU postprocessing and display routing tests PASS\n";
}

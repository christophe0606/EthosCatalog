// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace rvtts {
// One event-thread producer and one dataflow-thread consumer. Complete blocks
// are published with release/acquire ordering; neither side owns the other's
// current slot. Reset only between acknowledged utterances.
class PcmQueue {
public:
    static constexpr unsigned Samples = 160, Capacity = 4;
    void reset() { read_.store(0); write_.store(0); high_water_ = 0; }
    bool full() const {
        return write_.load(std::memory_order_relaxed) - read_.load(std::memory_order_acquire) == Capacity;
    }
    unsigned available() const {
        return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_relaxed);
    }
    bool push(const int16_t *pcm) {
        const uint32_t w = write_.load(std::memory_order_relaxed);
        const uint32_t r = read_.load(std::memory_order_acquire);
        if (w - r == Capacity) return false;
        std::memcpy(blocks_[w % Capacity], pcm, sizeof(blocks_[0]));
        high_water_ = std::max(high_water_, unsigned(w - r + 1));
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(int16_t *pcm) {
        const uint32_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        std::memcpy(pcm, blocks_[r % Capacity], sizeof(blocks_[0]));
        read_.store(r + 1, std::memory_order_release);
        return true;
    }
    unsigned highWater() const { return high_water_; } // Producer only.
private:
    int16_t blocks_[Capacity][Samples]{};
    std::atomic<uint32_t> read_{0}, write_{0};
    unsigned high_water_ = 0;
};

inline uint32_t pcmHash(uint32_t hash, const int16_t *pcm, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        const uint16_t sample = uint16_t(pcm[i]);
        hash = (hash ^ (sample & 255)) * 16777619u;
        hash = (hash ^ (sample >> 8)) * 16777619u;
    }
    return hash;
}

// Assemble FARGAN's first/last half frames without storing the utterance.
// A caller must reserve queue space with full() before generating each frame;
// append accepts at most one frame and therefore publishes at most one block.
class PcmWriter {
public:
    void reset() { used_ = samples = peak = nonzero = 0; hash = 2166136261u; }
    bool append(PcmQueue &queue, const int16_t *pcm, unsigned count) {
        if (count > PcmQueue::Samples || queue.full()) return false;
        hash = pcmHash(hash, pcm, count);
        for (unsigned i = 0; i < count; ++i) {
            const unsigned magnitude = pcm[i] < 0 ? unsigned(-int(pcm[i])) : unsigned(pcm[i]);
            peak = std::max(peak, magnitude);
            if (magnitude) ++nonzero;
            partial_[used_++] = pcm[i];
            ++samples;
            if (used_ == PcmQueue::Samples) {
                if (!queue.push(partial_)) return false;
                used_ = 0;
            }
        }
        return true;
    }
    bool complete() const { return used_ == 0; }
    uint32_t samples = 0, peak = 0, nonzero = 0, hash = 2166136261u;
private:
    int16_t partial_[PcmQueue::Samples]{};
    unsigned used_ = 0;
};
}

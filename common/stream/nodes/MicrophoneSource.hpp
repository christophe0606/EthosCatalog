// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"

template<typename OUT, int outputSamples>
class MicrophoneSource : public GenericSource<OUT, outputSamples> {
public:
    MicrophoneSource(FIFOBase<OUT>& out, const MicrophoneSourceParams& params)
        : GenericSource<OUT, outputSamples>(out), hw_(params.hw_) {
        static_assert(std::is_same_v<OUT,int16_t> && outputSamples > 0);
    }
    cg_status init() final {
        if (!hw_.microphone_start || !hw_.microphone_read || !hw_.stats || !hw_.microphone_block_samples)
            return CG_INIT_FAILURE;
        offset_ = hw_.microphone_block_samples;
        if (outputSamples % hw_.microphone_block_samples) {
            capture_.reset(new (std::nothrow) int16_t[hw_.microphone_block_samples]);
            if (!capture_) return CG_MEMORY_ALLOCATION_FAILURE;
        }
        return CG_SUCCESS;
    }
    int run() final {
        if (!started_) {
            int error = hw_.microphone_start();
            if (error) return error;
            started_ = true;
        }
        OUT* out = this->getWriteBuffer();
        size_t remaining = outputSamples;
        while (remaining) {
            if (offset_ == hw_.microphone_block_samples) {
                // Whole hardware blocks can be written directly to the FIFO.
                if (remaining >= hw_.microphone_block_samples) {
                    int error = hw_.microphone_read(out);
                    if (error) return error;
                    out += hw_.microphone_block_samples;
                    remaining -= hw_.microphone_block_samples;
                    continue;
                }
                int error = hw_.microphone_read(capture_.get());
                if (error) return error;
                offset_ = 0;
            }
            size_t count = std::min(remaining, hw_.microphone_block_samples - offset_);
            std::memcpy(out, capture_.get()+offset_, count*sizeof(OUT));
            out += count; offset_ += count; remaining -= count;
        }
        hw_.stats->audio_started = osKernelGetSysTimerCount();
        ++hw_.stats->audio_blocks;
        return CG_SUCCESS;
    }
private:
    HardwareParams hw_;
    std::unique_ptr<int16_t[]> capture_;
    size_t offset_ = 0;
    bool started_ = false;
};

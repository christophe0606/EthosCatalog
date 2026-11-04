// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace midi_ddsp {
constexpr std::array<uint32_t,256> crc32_table() {
    std::array<uint32_t,256> table{};
    for(unsigned i=0;i<256;++i){uint32_t c=i;for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320U&(0U-(c&1U)));table[i]=c;}
    return table;
}
inline uint32_t crc32(const uint8_t *data,size_t bytes) {
    static constexpr auto table=crc32_table();
    uint32_t c=~0U;
    for(size_t i=0;i<bytes;++i)c=(c>>8)^table[(c^data[i])&255U];
    return ~c;
}
}

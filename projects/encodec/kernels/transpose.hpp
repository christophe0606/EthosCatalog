// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
#include <arm_mve.h>
#endif

namespace encodec::cpu {
namespace detail {

#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
template<typename T>
std::size_t transposeVectorColumns(const T* input, T* output, std::size_t rows, std::size_t columns)
{
    const std::size_t vectorColumns = columns & ~std::size_t{3};
    if (rows == 2) {
        for (std::size_t col = 0; col < vectorColumns; col += 4) {
            const T* src = input + col;
            const decltype(vld2q(input)) vectors{{vld1q(src), vld1q(src + columns)}};
            vst2q(output + 2 * col, vectors);
        }
    } else if (rows == 4) {
        for (std::size_t col = 0; col < vectorColumns; col += 4) {
            const T* src = input + col;
            const decltype(vld4q(input)) vectors{{vld1q(src), vld1q(src + columns),
                                                  vld1q(src + 2 * columns), vld1q(src + 3 * columns)}};
            vst4q(output + 4 * col, vectors);
        }
    } else {
        if (rows > UINT32_MAX / (3 * sizeof(T))) return 0;
        const auto stride = static_cast<uint32_t>(rows);
        const uint32x4_t offsets{0, stride, 2 * stride, 3 * stride};
        for (std::size_t col = 0; col < vectorColumns; col += 4) {
            for (std::size_t row = 0; row < rows; ++row) {
                vstrwq_scatter_shifted_offset(output + col * rows + row, offsets,
                                              vld1q(input + row * columns + col));
            }
        }
    }
    return vectorColumns;
}
#endif

} // namespace detail

// Both buffers span batch * rows * columns elements and are disjoint.
template<typename T>
void transposeLastTwo(const T* input, T* output, std::size_t batch, std::size_t rows, std::size_t columns)
{
    static_assert(sizeof(T) == 4 && std::is_trivially_copyable_v<T>);
    if (batch == 0 || rows == 0 || columns == 0) return;

    const std::size_t matrixSize = rows * columns;
    if (rows == 1 || columns == 1) {
        std::memcpy(output, input, batch * matrixSize * sizeof(T));
        return;
    }

    for (std::size_t matrix = 0; matrix < batch; ++matrix) {
        const T* src = input + matrix * matrixSize;
        T* dst = output + matrix * matrixSize;
        std::size_t col = 0;
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
        if constexpr (std::is_same_v<T, float> || std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t>) {
            col = detail::transposeVectorColumns(src, dst, rows, columns);
        }
#endif
        for (; col < columns; ++col) {
            for (std::size_t row = 0; row < rows; ++row) {
                std::memcpy(dst + col * rows + row, src + row * columns + col, sizeof(T));
            }
        }
    }
}

} // namespace encodec::cpu

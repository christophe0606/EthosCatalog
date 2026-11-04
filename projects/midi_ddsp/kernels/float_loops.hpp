// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace midi_ddsp::kernels {
constexpr int max_rank = 8;
struct Shape {
    int rank{};
    int64_t extent[max_rank]{};
    size_t elements() const {
        size_t n = 1;
        for (int d = 0; d < rank; ++d) n *= size_t(extent[d]);
        return n;
    }
};
// Accept only an identical contiguous suffix, with leading singleton axes.
// This excludes interleaved broadcasts such as [C,1] over [C,T].
inline bool repeated_suffix(const Shape &a, const Shape &out) {
    if (a.rank > out.rank) return false;
    bool varying = false;
    for (int d = 0; d < out.rank; ++d) {
        const int ad = d - (out.rank - a.rank);
        const int64_t size = ad < 0 ? 1 : a.extent[ad];
        if (size < 1 || out.extent[d] < 1) return false;
        if (size != 1) varying = true;
        if (varying ? size != out.extent[d] : size != 1) return false;
    }
    return true;
}
// A contiguous prefix followed by singleton axes holds one scalar per row,
// such as [B,1] over [B,C]. Broadcasts within the prefix are unsupported.
inline size_t row_broadcast_inner(const Shape &a,const Shape &out) {
    if (a.rank>out.rank) return 0;
    int last=-1;
    for (int d=0;d<out.rank;++d) {
        const int ad=d-(out.rank-a.rank);
        const int64_t size=ad<0?1:a.extent[ad];
        if (size<1 || out.extent[d]<1) return 0;
        if (size!=1) last=d;
    }
    size_t inner=1;
    for (int d=0;d<out.rank;++d) {
        const int ad=d-(out.rank-a.rank);
        const int64_t size=ad<0?1:a.extent[ad];
        if (d<=last && size!=out.extent[d]) return 0;
        if (d>last) inner*=size_t(out.extent[d]);
    }
    return inner;
}
enum class Binary { add, subtract, multiply };
inline void binary_rows(const float *full,const float *rows,float *out,
                        size_t count,size_t inner,bool rows_first,
                        Binary op,float alpha=1) {
    for (size_t base=0,row=0;base<count;base+=inner,++row) {
        const float scalar=rows[row];
        for (size_t j=0;j<inner;++j) {
            const float x=rows_first?scalar:full[base+j];
            const float y=rows_first?full[base+j]:scalar;
            out[base+j]=op==Binary::multiply?x*y
                      :op==Binary::subtract?x-alpha*y:x+alpha*y;
        }
    }
}
inline void binary(const float *a, size_t na, const float *b, size_t nb,
                   float *out, size_t count, Binary op, float alpha = 1) {
    // Block loops keep division/modulo and generic index iterators out of the
    // inner loop. Singleton operands and learned channel vectors are common.
    const size_t period = na == 1 ? nb : nb == 1 ? na : na < nb ? na : nb;
    for (size_t base = 0; base < count; base += period) {
        const float *aa = a + base%na;
        const float *bb = b + base%nb;
        for (size_t j = 0; j < period; ++j) {
            const float x = aa[na == 1 ? 0 : j];
            const float y = bb[nb == 1 ? 0 : j];
            out[base+j] = op == Binary::multiply ? x*y
                        : op == Binary::subtract ? x-alpha*y : x+alpha*y;
        }
    }
}
inline void mean_tail(const float *in, float *out, size_t outer, size_t inner) {
    for (size_t row = 0; row < outer; ++row) {
        const float *x = in + row*inner;
        // Independent accumulators reduce long float32 accumulation error and
        // permit vectorization without introducing software float64 arithmetic.
        float s[8]{};
        size_t j = 0;
        for (; j+8 <= inner; j += 8)
            for (size_t k = 0; k < 8; ++k) s[k] += x[j+k];
        float total = ((s[0]+s[1])+(s[2]+s[3]))+((s[4]+s[5])+(s[6]+s[7]));
        for (; j < inner; ++j) total += x[j];
        out[row] = total / float(inner);
    }
}
inline void square(const float *in, float *out, size_t count) {
    for (size_t j = 0; j < count; ++j) out[j] = in[j]*in[j];
}
inline void reciprocal_sqrt(const float *in, float *out, size_t count) {
    for (size_t j = 0; j < count; ++j) out[j] = 1.0f/std::sqrt(in[j]);
}
} // namespace midi_ddsp::kernels

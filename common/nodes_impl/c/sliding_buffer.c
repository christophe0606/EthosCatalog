// SPDX-License-Identifier: Apache-2.0
#include "SlidingBuffer.hpp"
#include <string.h>
void sliding_buffer_init(sliding_buffer_t *s, float *storage, size_t size)
{
    s->data = storage; s->size = size; s->valid = 0;
    memset(storage, 0, size * sizeof(float));
}
bool sliding_buffer_push(sliding_buffer_t *s, const float *block, size_t count)
{
    if (count > s->size) { block += count - s->size; count = s->size; }
    memmove(s->data, s->data + count, (s->size - count) * sizeof(float));
    memcpy(s->data + s->size - count, block, count * sizeof(float));
    s->valid += count;
    if (s->valid > s->size) s->valid = s->size;
    return s->valid == s->size;
}

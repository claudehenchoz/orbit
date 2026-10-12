/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include "../firmware/src/op1_cluster_core.c"

static int64_t reference_asr(int64_t x, unsigned n)
{
    int64_t d = INT64_C(1) << n;
    return x >= 0 ? x / d : -1 - (-(x + 1) / d);
}

static int16_t reference_mul(int16_t a, int16_t b)
{
    int64_t p = (int64_t)a * b * 2;
    if (p == INT64_C(2147483648))
        p = INT32_MAX;
    int64_t h = reference_asr(p, 16);
    uint32_t l = (uint32_t)p & 65535u;
    if (l > 32768u || (l == 32768u && h % 2 != 0))
        ++h;
    return (int16_t)(h > 32767 ? 32767 : h < -32768 ? -32768 : h);
}

static uint32_t reference_detune(uint32_t phase, int16_t control)
{
    int32_t high = (int32_t)(phase >> 16);
    if (high >= 32768)
        high -= 65536;
    int64_t v = (int64_t)high * control * 2 +
                reference_asr((int64_t)control * (phase & 65535u), 15);
    return (uint32_t)(v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v);
}

int main(void)
{
    static const int16_t edge[] = {-32768, -32767, -16385, -16384, -1, 0, 1, 16383, 16384, 32766, 32767};
    static const uint32_t phase_edge[] = {
        0, 1, 65535, 65536, UINT32_C(0x7fff0000), UINT32_C(0x7fffffff),
        UINT32_C(0x80000000), UINT32_C(0x80000001), UINT32_C(0x8000ffff),
        UINT32_C(0xffff0000), UINT32_MAX
    };
    static const int64_t wide[] = {INT64_MIN, INT64_MIN + 1, INT32_MIN, -65537, -65536, -1,
                                  0, 1, 65535, 65536, INT32_MAX, INT64_MAX};
    static const unsigned shift[] = {0, 3, 10, 11, 15, 16};
    uint32_t seed = 1, i, j, count = 0, detunes = 0;
    for (i = 0; i < sizeof wide / sizeof wide[0]; i++)
        for (j = 0; j < sizeof shift / sizeof shift[0]; j++)
            assert(cls_asr(wide[i], shift[j]) == reference_asr(wide[i], shift[j]));
    for (int32_t a = INT16_MIN; a <= INT16_MAX; a++)
        for (j = 0; j < sizeof edge / sizeof edge[0]; j++) {
            assert(cls_mul((int16_t)a, edge[j]) == reference_mul((int16_t)a, edge[j]));
            count++;
        }
    for (int32_t control = INT16_MIN; control <= INT16_MAX; control++)
        for (j = 0; j < sizeof phase_edge / sizeof phase_edge[0]; j++) {
            assert(cls_detune(phase_edge[j], (int16_t)control) == reference_detune(phase_edge[j], (int16_t)control));
            detunes++;
        }
    for (i = 0; i < 1000000u; i++) {
        seed = seed * 1664525u + 1013904223u;
        int16_t a = (int16_t)cls_s16(seed >> 16);
        seed = seed * 1664525u + 1013904223u;
        int16_t b = (int16_t)cls_s16(seed >> 16);
        assert(cls_mul(a, b) == reference_mul(a, b));
        assert(cls_detune(seed, a) == reference_detune(seed, a));
        detunes++;
        for (j = 0; j < sizeof shift / sizeof shift[0]; j++)
            assert(cls_asr((int64_t)a * seed, shift[j]) == reference_asr((int64_t)a * seed, shift[j]));
        count++;
    }
    printf("PASS: CLUSTER arithmetic matches division oracle over %u products and signed shift boundaries\n", count);
    printf("PASS: CLUSTER phase/control matches signed-wide oracle over %u pairs\n", detunes);
    return 0;
}

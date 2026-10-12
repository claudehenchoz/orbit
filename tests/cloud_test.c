/* SPDX-License-Identifier: GPL-3.0-only */
#define ARENA_STATS 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? ORBIT_FM4 : 0u; }
#include "../firmware/src/project.c"

typedef union {
    gr_part_t grain;
    fm6_note_t fm6[FM6_POLY];
    uint32_t phys[PHYS_ARENA / 4u];
    cluster_part_t cluster;
} old_arena_t;

static void reset_case(void)
{
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(eng_arena_own, 0, sizeof eng_arena_own);
    eng_arena_bad = 0;
    host_tracks_init();
}

static uint64_t render_case(uint32_t pi, const int16_t *knobs, uint32_t note)
{
    track_t *t = &trk[0];
    uint32_t b, i, audible = 0, peak = 0;
    double energy = 0;
    uint64_t hash = 0xcbf29ce484222325ull;
    int32_t out[CTL + 2];
    /* Given: an actual preset and one real allocator/envelope voice. */
    host_preset(t, ENGI_CLOUD, pi);
    if (knobs)
        memcpy(&t->p[P_E0], knobs, 8u * sizeof *knobs);
    t->p[P_ATK] = 0;
    trk_note_on(t, note, 100);
    /* When: render three seconds, then release through the real envelope. */
    for (b = 0; b < 4096u; b++) {
        out[0] = 123456789;
        out[CTL + 1] = 987654321;
        track_render(t, out + 1, CTL);
        /* Then: guards, pool bounds and a finite, audible fixed-point signal hold. */
        assert(out[0] == 123456789 && out[CTL + 1] == 987654321);
        for (i = 0; i < CTL; i++) {
            int32_t x = out[i + 1];
            uint32_t a = (uint32_t)abs(x);
            assert(isfinite((double)x) && a <= 72000u);
            audible += x != 0;
            if (a > peak)
                peak = a;
            energy += (double)x * x;
            hash = (hash ^ (uint32_t)x) * 0x100000001b3ull;
        }
        for (i = 0; i < CLOUD_NG; i++) {
            cloud_grain_t *g = &eng_arena[0].cloud.g[i];
            assert(g->owner <= NVOICE);
            if (g->owner) {
                assert(g->inc <= CLOUD_INC_MAX);
                assert(g->left <= 18000u);
            }
        }
    }
    assert(audible && peak > 100u && energy > 1e6);
    if (!knobs) {
        double rms = sqrt(energy / (4096u * CTL));
        assert(rms > 300.0 && rms < 18000.0);
        printf("%s: peak=%u rms=%.1f\n", CLOUD_PRESETS[pi].name, peak, rms);
    }
    trk_all_off(t);
    for (b = 0; b < 16000u; b++)
        track_render(t, out + 1, CTL);
    for (i = 0; i < NVOICE; i++)
        assert(!t->v[i].active);
    for (i = 0; i < CLOUD_NG; i++)
        assert(!eng_arena[0].cloud.g[i].owner);
    for (i = 0; i < CTL; i++)
        assert(out[i + 1] == 0);
    assert(!eng_arena_bad);
    return hash;
}

static void owner_case(void)
{
    track_t *t = &trk[0];
    cloud_part_t *p;
    vmod_t m = {.inc = 0x00800000u, .amp0 = 32767, .amp1 = 32767, .shape = 64 << 8};
    cloud_grain_t saved[CLOUD_NG];
    uint32_t i, b, counts[NVOICE] = {0};
    int32_t out[CTL] = {0};
    /* Given: three voices at maximum overlap, sharing a bounded twelve-grain pool. */
    reset_case();
    host_preset(t, ENGI_CLOUD, 0);
    t->p[P_E1] = t->p[P_E2] = 127;
    for (i = 0; i < 3u; i++)
        trk_note_on(t, 48u + i * 7u, 100);
    assert(trk_nvoice(t) == 3u);
    for (b = 0; b < 1000u; b++)
        track_render(t, out, CTL);
    p = eng_arena_of(t, ENGI_CLOUD);
    for (i = 0; i < CLOUD_NG; i++)
        if (p->g[i].owner)
            counts[p->g[i].owner - 1u]++;
    for (i = 0; i < 3u; i++)
        assert(counts[i] == 4u);
    memcpy(saved, p->g, sizeof saved);
    /* When: render only voice zero. */
    cloud_render(t, &t->v[0], out, CTL, &m);
    /* Then: no grain belonging to another voice is advanced or overwritten. */
    for (i = 0; i < CLOUD_NG; i++)
        if (saved[i].owner != 1u)
            assert(!memcmp(&saved[i], &p->g[i], sizeof saved[i]));
    t->v[0].active = 0;
    cloud_block(t);
    for (i = 0; i < CLOUD_NG; i++)
        assert(p->g[i].owner != 1u);
}

int main(void)
{
    uint32_t pi, k, mask;
    uint64_t fresh, lo, hi;
    int16_t knobs[8];
    static const uint32_t previous[] = {ENGI_GRAIN, ENGI_FM6, ENGI_PHYS, ENGI_CLUSTER};
    int32_t out[CTL];
    _Static_assert(sizeof(project_t) == 3840u, "FUN5 unchanged");
    _Static_assert(sizeof(eng_arena_t) == sizeof(old_arena_t), "zero arena growth");
    _Static_assert(ENGI_CLOUD == 16u + FELUCCA_SLICE, "append-only CLOUD");
    assert(ENGINES[ENGI_CLOUD] == &ENG_CLOUD && ENG_CLOUD.poly == 3u);
    assert(ENG_CLOUD.npresets == 8u && ENG_GRAIN.npresets == 8u);
    assert(ENG_GRAIN.edit[0].max == SMP_NALL - 1 && ENG_GRAIN.poly == 3u);
    printf("host sizeof: old_arena=%zu new_arena=%zu grain=%zu cloud=%zu; pool growth=0\n",
           sizeof(old_arena_t), sizeof(eng_arena_t), sizeof(gr_part_t), sizeof(cloud_part_t));
    for (pi = 0; pi < 8u; pi++) {
        reset_case();
        render_case(pi, 0, 60);
    }
    /* Each control must affect sound, independently of its neighbors. */
    for (k = 0; k < 8u; k++) {
        for (pi = 0; pi < 8u; pi++)
            knobs[pi] = CLOUD_PRESETS[0].e[pi];
        knobs[k] = ENG_CLOUD.edit[k].min;
        reset_case();
        lo = render_case(0, knobs, 60);
        knobs[k] = ENG_CLOUD.edit[k].max;
        reset_case();
        hi = render_case(0, knobs, 60);
        assert(lo != hi);
    }
    /* Every combination of eight extrema at both ends of the MIDI pitch range. */
    for (mask = 0; mask < 256u; mask++)
        for (pi = 0; pi < 2u; pi++) {
            for (k = 0; k < 8u; k++)
                knobs[k] = mask & (1u << k) ? ENG_CLOUD.edit[k].max : ENG_CLOUD.edit[k].min;
            reset_case();
            render_case(0, knobs, pi ? 127u : 0u);
        }
    owner_case();
    /* Given: fresh CLOUD output, and each older arena engine leaving live state behind. */
    reset_case();
    fresh = render_case(0, 0, 60);
    for (k = 0; k < NELEM(previous); k++) {
        reset_case();
        host_preset(&trk[0], previous[k], 0);
        trk_note_on(&trk[0], 60, 100);
        for (pi = 0; pi < 256u; pi++)
            track_render(&trk[0], out, CTL);
        assert(eng_arena_own[0] == previous[k] + 1u);
        memset(&trk[0], 0, sizeof trk[0]);
        host_tracks_init();
        /* When: the new engine claims the same arena. Then: deterministic clean takeover. */
        assert(render_case(0, 0, 60) == fresh);
    }
    puts("PASS: 8 presets, 8 effective controls, 512 pitch/extrema cases, release, guards, owners, takeover");
    return 0;
}

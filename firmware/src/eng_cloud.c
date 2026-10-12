/* SPDX-License-Identifier: GPL-3.0-only */
/* CLOUD: original oscillator-source grains, not sample playback or recorded audio.
 * WAVE morphs sine -> triangle -> a three-harmonic sine spectrum. SIZE is 10..390 ms;
 * DENS is 1..98 starts/s/voice; SPRD jitters timing and starting phase; DTUN spreads
 * pitch by up to two semitones. CHRP sweeps up/down by up to two octaves per grain.
 * STCK adds fifths and octaves probabilistically. TONE follows the shared FLT modulation.
 * Twelve grains are shared fairly by three voices. All divides and pitch ratios are
 * computed at grain/block boundaries; the sample loop only advances fixed-point ramps.
 */
#define CLOUD_NG 12u
#define CLOUD_INC_MAX 0x18000000u  /* bound all three harmonics below Nyquist */
typedef struct {
    uint32_t ph, inc, wph, winc, left;
    int32_t chirp, gain;
    uint8_t owner, wave;
} cloud_grain_t;
typedef struct {
    cloud_grain_t g[CLOUD_NG];
    int32_t rng;
} cloud_part_t;

static void cloud_note_on(track_t *t, voice_t *v)
{
    cloud_part_t *p = eng_arena_of(t, ENGI_CLOUD);
    uint32_t i, owner = (uint32_t)(v - t->v) + 1u;
    if (!p->rng)
        p->rng = (int32_t)(0x2545F491u ^ ((uint32_t)(t - trk) * 0x9E3779B9u));
    v->s[1] = 0;
    if (!v->env) {
        v->s[2] = 0;
        for (i = 0; i < CLOUD_NG; i++)
            if (p->g[i].owner == owner)
                p->g[i].owner = 0;
    }
}

static void cloud_block(track_t *t)
{
    cloud_part_t *p = eng_arena_of(t, ENGI_CLOUD);
    uint32_t i;
    for (i = 0; i < CLOUD_NG; i++)
        if (p->g[i].owner && !t->v[p->g[i].owner - 1u].active)
            p->g[i].owner = 0;
}

static void cloud_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    cloud_part_t *p = eng_arena_of(t, ENGI_CLOUD);
    uint32_t i, k, owner = (uint32_t)(v - t->v) + 1u, mine = 0, active = 0;
    uint32_t iv = (FS * pow2_q16(-t->p[P_E2] * 10)) >> 16;
    int32_t acc[CTL] = {0}, y = v->s[2];
    int32_t lp = 4000 + ((clamp((t->p[P_E7] << 8) + m->cutoff, 0, 127 << 8) * 28767) >> 15);
    if (n > CTL)
        n = CTL;
    for (k = 0; k < NVOICE; k++)
        active += t->v[k].active != 0;
    for (k = 0; k < CLOUD_NG; k++) {
        cloud_grain_t *g = &p->g[k];
        uint32_t count;
        int32_t w0, w1, wq, dq;
        if (g->owner != owner)
            continue;
        count = n < g->left ? n : g->left;
        w0 = mulq15(gr_win(g->wph), g->gain);
        g->wph += g->winc * count;
        g->left -= count;
        w1 = g->left ? mulq15(gr_win(g->wph), g->gain) : 0;
        wq = w0 * 1024;
        dq = count == CTL ? ((w1 - w0) * 1024) >> CTL_LOG2 :
             count ? (w1 - w0) * 1024 / (int32_t)count : 0;
        for (i = 0; i < count; i++) {
            int32_t a = osc_tri(g->ph), b;
            if (g->wave <= 64u) {
                b = osc_sine(g->ph);
                a = b + mulq15(a - b, g->wave * 512);
            } else {
                b = (osc_sine(g->ph) >> 1) + (osc_sine(g->ph * 2u) >> 2) +
                    (osc_sine(g->ph * 3u) >> 2);
                a += mulq15(b - a, (g->wave - 64u) * 520);
            }
            acc[i] += mulq15(a, wq >> 10);
            wq += dq;
            g->ph += g->inc;
            g->inc = (uint32_t)((int32_t)g->inc + g->chirp);
        }
        if (!g->left)
            g->owner = 0;
        else
            mine++;
    }
    v->s[1] -= (int32_t)n;
    if (v->s[1] <= 0) {
        uint32_t r = noise32(&p->rng), share = CLOUD_NG / (active ? active : 1u);
        int32_t jitter = (((int32_t)(r & 65535u) - 32768) * t->p[P_E3]) / 254;
        v->s[1] += (int32_t)iv + mulq15((int32_t)iv, jitter);
        if (v->s[1] < 0)
            v->s[1] = 0;
        for (k = 0; k < CLOUD_NG && mine < share; k++) {
            cloud_grain_t *g = &p->g[k];
            uint32_t len, overlap, end;
            uint64_t start64, end64;
            int32_t detune, stack = 0, sweep = t->p[P_E5] * 3;
            if (g->owner)
                continue;
            len = (441u * pow2_q16(t->p[P_E1] * 8)) >> 16;
            detune = (((int32_t)((r >> 16) & 65535u) - 32768) * t->p[P_E4] * 32) /
                     (127 * 32768);
            if ((r & 127u) < (uint32_t)t->p[P_E6])
                stack = (r & 128u) ? 192 : 112;
            start64 = ((uint64_t)m->inc * pow2_q16(detune + stack - sweep)) >> 16;
            end64 = ((uint64_t)m->inc * pow2_q16(detune + stack + sweep)) >> 16;
            g->inc = start64 > CLOUD_INC_MAX ? CLOUD_INC_MAX : (uint32_t)start64;
            end = end64 > CLOUD_INC_MAX ? CLOUD_INC_MAX : (uint32_t)end64;
            g->chirp = ((int32_t)end - (int32_t)g->inc) / (int32_t)len;
            g->ph = gr_scale(r, (uint32_t)t->p[P_E3] * 516u);
            g->wph = 0;
            g->winc = 0xFFFFFFFFu / len;
            g->left = len;
            overlap = len / iv + 1u;
            if (overlap > share)
                overlap = share;
            g->gain = GR_NORM[overlap];
            g->wave = (uint8_t)clamp(t->p[P_E0] + ((m->shape - (64 << 8)) >> 8), 0, 127);
            g->owner = (uint8_t)owner;
            break;
        }
    }
    for (i = 0; i < n; i++) {
        y += mulq15(clamp(acc[i] - y, -65535, 65535), lp);
        out[i] += voice_amp(soft_knee(y, 32767), m, i) * 2;
    }
    v->s[2] = y;
}

static const preset_t CLOUD_PRESETS[] = {
    /* WAVE SIZE DENS SPRD DTUN CHRP STCK TONE; original oscillator-only sounds. */
    {"CIRRUS",      {0, 100, 100, 55, 18, 0, 0, 100}, {45, 90, 120, 70}, 0, 0, FX(0, 20, 20, 50)},
    {"SILVER MIST", {42, 110, 110, 90, 40, 4, 30, 105}, {65, 90, 120, 80}, 0, 0, FX(0, 30, 25, 60)},
    {"GLASS RAIN",  {110, 24, 95, 100, 65, -22, 75, 120}, {0, 70, 100, 45}, 0, 0, FX(0, 15, 30, 45)},
    {"EMBER",       {70, 90, 115, 45, 12, 0, 0, 40}, {25, 90, 127, 60}, 0, 0, FX(0, 10, 10, 35)},
    {"SKY FIFTHS",  {20, 100, 108, 70, 15, 0, 110, 110}, {40, 90, 120, 65}, 0, 0, FX(0, 15, 25, 50)},
    {"COMET TAIL",  {100, 75, 100, 60, 35, 40, 40, 115}, {5, 90, 110, 65}, 0, 0, FX(0, 10, 35, 55)},
    {"DUST PULSE",  {64, 8, 55, 25, 5, -8, 0, 100}, {0, 60, 127, 30}, 0, 0, FX(0, 0, 15, 20)},
    {"LOW STRATUS", {35, 120, 115, 100, 22, -3, 10, 65}, {55, 100, 127, 80}, 0, 0, FX(0, 20, 15, 45)},
};
static const engine_t ENG_CLOUD = {
    .name = "CLOUD", .page_title = {"GRAINS", "SPECTRUM"},
    .edit = {
        {"WAVE", F_PCT, 0, 127, 0, 0, 0},
        {"SIZE", F_PCT, 0, 127, 100, 0, 0},
        {"DENS", F_PCT, 0, 127, 100, 0, 0},
        {"SPRD", F_PCT, 0, 127, 55, 0, 0},
        {"DTUN", F_PCT, 0, 127, 18, 0, 0},
        {"CHRP", F_BIPCT, -64, 63, 0, 0, 0},
        {"STCK", F_PCT, 0, 127, 0, 0, 0},
        {"TONE", F_PCT, 0, 127, 100, 0, 0},
    },
    .presets = CLOUD_PRESETS, .npresets = NELEM(CLOUD_PRESETS), .fil_page = 1,
    .note_on = cloud_note_on, .render = cloud_render, .color = 0x87F0,
    .macro = {P_E0, P_E1, P_E2, P_E3}, .poly = 3, .block = cloud_block,
};

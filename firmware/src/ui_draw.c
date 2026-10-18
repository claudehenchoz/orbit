/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca UI drawing: status bar (top), columns + gauges, graphs, focus readout,
 * footer (steps + engine / preset / page). */
static void draw_menu(void);
static uint32_t str_hash(uint32_t h, const char *s);

/* --------------------------------------------------------- drawing --- */
static int is_eng_name(const char *s)                 /* one of the ENGINES[]->name strings */
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (s == ENGINES[e]->name)
            return 1;
    return 0;
}

/* at most 5 characters, and no wider than maxw */
static void fit(char *d, const char *src, const felucca_font_t *f, int32_t maxw)
{
    str_cpy(d, src, is_eng_name(src) ? 8 : 6);           /* engine names are kept whole */
    while (d[0] && text_w(f, d) > maxw)
        d[str_len(d) - 1u] = 0;
}

static int32_t batt_level(void)                         /* ADC ch3 thresholds */
{
    return song.batt_raw >= 591 ? 3 : song.batt_raw >= 561 ? 2 : song.batt_raw >= 531 ? 1 : 0;
}
/* charging (a USB host powers us; there is no charger status line): bars fill 1, 2, 3
 * every 0.6 s, so the header is redrawn twice a second at most; else the level */
static int32_t batt_shown(void)
{
    if (usb.config && !usb.suspended)
        return 1 + (int32_t)((fm1_ms / 600u) % 3u);
    return batt_level();
}

/* 현재 헤더 canvas에 기존 잔량 아이콘을 그린다. ORBIT 헤더는 USB 글자 대신 충전 애니메이션만 사용한다. */
static void draw_battery(int usb_label)
{
    int32_t lvl = batt_shown(), k, bx = 217;
    cv_rect(bx, 4, 17, 1, C_GRAY);
    cv_rect(bx, 12, 17, 1, C_GRAY);
    cv_rect(bx, 4, 1, 9, C_GRAY);
    cv_rect(bx + 16, 4, 1, 9, C_GRAY);
    cv_rect(bx + 17, 6, 2, 5, C_GRAY);
    for (k = 0; k < lvl; k++)
        cv_rect(bx + 2 + k * 5, 6, 3, 5, lvl == 1 && batt_level() <= 1 ? C_WHITE : C_HI);
    if (usb_label && usb.config && !usb.suspended)
        cv_text(bx - 28, 1, &FONT_S, "USB", C_DIM);
}

static int big_page(const page_t *pg);
/* column c: what KNOB c+1 does here: its label, value and unit, the colour (white while turned, dim
 * when it does nothing now), how far it is turned (0..1000, -1: not an amount). The knob strip
 * (gen_strip) and the big dials (graph_big) draw them; nothing is drawn here */
static int16_t col_ratio[4];
static void draw_column(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc,
                        int32_t ratio, uint32_t icon)
{
    (void)icon;
    if (c >= 4u)
        return;
    str_cpy(ui.big_l[c], label, sizeof ui.big_l[c]);
    str_cpy(ui.big_v[c], val, sizeof ui.big_v[c]);
    str_cpy(ui.big_u[c], unit, sizeof ui.big_u[c]);
    ui.big_c[c] = vc;
    col_ratio[c] = (int16_t)ratio;
}

static void graph_lfo(const track_t *t, uint16_t c)
{
    int32_t x, py = CV_AA ? 50 * 16 + 8 : 50;
    uint32_t ph = (uint32_t)t->p[P_LPHASE] << 25;
    for (x = 0; x < 240; x++) {                      /* (lfo_wave only reads the track) */
#if CV_AA   /* 1/16 px */
        int32_t y = 50 * 16 + 8 - lfo_wave((track_t *)t, ph + (uint32_t)x * (0xFFFFFFFFu / 120u)) * 38 / 2048;
        if (t->p[P_LWAVE] == 4)
            y = 50 * 16 + 8 - ((int32_t)((x / 20 * 2654435761u) >> 16) - 32768) * 38 / 2048;
        if (!x)
            cv_curve_begin(22, c);
        cv_curve_pt(x, y);
#else
        int32_t y = 50 - lfo_wave((track_t *)t, ph + (uint32_t)x * (0xFFFFFFFFu / 120u)) * 38 / 32768;
        if (t->p[P_LWAVE] == 4)
            y = 50 - ((int32_t)((x / 20 * 2654435761u) >> 16) - 32768) * 38 / 32768;
        if (x)
            cv_line(x - 1, py, x, y, c);
#endif
        py = y;
    }
#if CV_AA
    cv_curve_end();
#endif
    cv_line(0, 50, 239, 50, C_LINE);
}

static void graph_steps(const track_t *t, uint16_t c)
{
    uint32_t i, len = (uint32_t)t->p[P_SLEN];
    for (i = 0; i < NSTEP; i++) {                    /* 4 rows of 16 thin bars */
        int32_t x = 6 + (int32_t)(i % 16u) * 14 + (int32_t)(i % 16u) / 4 * 4, y = 6 + (int32_t)(i / 16u) * 24;
        const step_t *st = &t->step[i];
        if (i >= len)
            continue;
        if (step_on(st))
            cv_rect(x, y, 1, 14, c);
        else if (st->time == ST_TIE)
            cv_rect(x, y + 6, 1, 8, C_DIM);
        else
            cv_rect(x, y + 13, 1, 1, C_DIM);
        if (st->flags & SF_ACCENT)
            cv_rect(x - 1, y - 2, 3, 1, c);
        if ((song.playing && i == t->seq_idx) || i == ui.cursor)
            cv_rect(x - 1, y + 16, 3, 3, C_WHITE);
    }
}
/* STEP page: the cursor's 16-step bank as a little piano roll */
static void graph_roll(const track_t *t, uint16_t c)
{
    uint32_t i, j, len = (uint32_t)t->p[P_SLEN], base = ui.bank * 16u;
    int32_t lo = 127, hi = 0, prev_y = -1;
    for (i = 0; i < len; i++)
        for (j = 0; j < t->step[i].n; j++) {
            if (t->step[i].note[j] < lo)
                lo = t->step[i].note[j];
            if (t->step[i].note[j] > hi)
                hi = t->step[i].note[j];
        }
    if (lo > hi) {
        lo = 48;
        hi = 72;
    }
    if (hi - lo < 12)
        hi = lo + 12;
    for (i = 0; i < 16u; i++) {
        uint32_t si = base + i;
        const step_t *st = &t->step[si];
        int32_t x = (int32_t)i * 15, yb = 86;
        if (si >= len)
            break;
        if (si == ui.cursor)
            cv_rect(x + 5, 92, 3, 3, C_WHITE);
        if (song.playing && si == t->seq_idx)
            cv_rect(x + 1, 97, 12, 1, C_WHITE);
        if (st->time == ST_TIE && prev_y >= 0) {
            cv_rect(x, prev_y, 14, 1, C_GRAY);
            continue;
        }
        if (!step_on(st)) {
            cv_rect(x + 6, yb, 2, 1, C_DIM);
            prev_y = -1;
            continue;
        }
        for (j = 0; j < st->n; j++) {
            int32_t y = 80 - (st->note[j] - lo) * 74 / (hi - lo);
            cv_rect(x + 2, y, 10, 1, (st->flags & SF_ACCENT) ? C_WHITE : c);
            if (j == 0)
                prev_y = y;
        }
        if (st->flags & SF_SLIDE)
            cv_line(x + 11, prev_y, x + 17, prev_y + 2, c);
    }
}
static void graph_scale(const track_t *t, uint16_t c)
{
    static const uint8_t BLACK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    uint32_t i, mask = scale_mask(t);
    for (i = 0; i < 12u; i++) {
        uint32_t deg = (i + 12u - (uint32_t)t->p[P_ROOT]) % 12u;
        int32_t x = 6 + (int32_t)i * 19;
        uint16_t col = (mask >> deg) & 1u ? (deg == 0u ? C_WHITE : c) : C_DIM;
        cv_rect(x, BLACK[i] ? 10 : 40, 16, 1, col);
        cv_rect(x + 7, BLACK[i] ? 10 : 40, 1, 40, col);
    }
}

/* SLICER page: the pattern's 16 steps, a 'x' step a full bar; a '.' step: GATE a bar as high as
 * it stays open (DEPTH), STUT hatched (it repeats the last 'x'); the step playing underlined.
 * Grey when the SLICER is OFF. */
static void graph_slicer(const track_t *t, uint16_t c)
{
    uint32_t i, pat = sl_pattern(t), mode = (uint32_t)t->p[P_SLCR], cur = sl[t - trk].idx;
    int32_t open = 70 - t->p[P_SLDEPTH] * 70 / 127;      /* px a closed GATE step keeps */
    uint16_t col = mode == SL_OFF ? C_DIM : c;
    for (i = 0; i < 16u; i++) {
        int32_t x = 4 + (int32_t)i * 14 + (int32_t)(i / 4u) * 2, y;
        if ((pat >> i) & 1u) {
            cv_rect(x, 10, 11, 70, col);
        } else if (mode == SL_STUT) {
            for (y = 10; y < 80; y += 4)
                cv_rect(x, y, 11, 1, col);
        } else {
            cv_rect(x, 79, 11, 1, C_LINE);
            if (open)
                cv_rect(x, 80 - open, 11, open, C_DIM);
        }
        if (mode != SL_OFF && i == cur)
            cv_rect(x, 85, 11, 3, C_WHITE);
    }
}
static uint32_t steps_hash(const track_t *t)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < NSTEP; i++) {
        const step_t *st = &t->step[i];
        h = (h ^ (st->note[0] + st->n * 128u + st->time * 1024u + st->flags * 4096u + st->note[1] * 65536u)) *
            16777619u;
    }
    return h;
}

static uint32_t str_hash(uint32_t h, const char *s)
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

/* a page drawn with its values large (graph_big) */
static int big_page(const page_t *pg)            /* (2.6: the INSERT pages too: their middle stayed empty) */
{
    return (pg->graph == GR_NONE || pg->graph == GR_INS || pg->graph == GR_PERFORM) &&
           pg->scope != SC_SONG && pg->scope != SC_DRUM;
}
static uint32_t graph_signature(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint32_t h = 2166136261u, i;
    if (big_page(pg))                                /* the big values: as the columns show them */
        for (i = 0; i < 4u; i++)
            h = str_hash(str_hash(str_hash(h ^ ui.big_c[i] * 31u, ui.big_v[i]), ui.big_l[i]), ui.big_u[i]);
    h ^= (uint32_t)pg->graph * 131u + TSEL->eng_req + song.sel * 7777u;
    for (i = 0; i < P_COUNT; i++)
        h = (h ^ (uint32_t)t->p[i]) * 16777619u;
    h ^= (uint32_t)TSEL->preset * 7u + (uint32_t)song.g[G_SLOT] * 13u + TSEL->user * 257u + up_gen * 7919u + ui.uslot * 104729u;
    if (pg->graph == GR_SLCR && t->p[P_SLCR])        /* the SLICER's step playing */
        h ^= (sl[song.sel].idx + 1u) * 2654435761u;
    if (pg->graph == GR_SLOTS)                       /* (a checksum over each slot) */
        for (i = 0; i < 4u; i++)
            h ^= (uint32_t)project_used(i) << (20u + i);
    if (pg->graph == GR_STEPS || pg->graph == GR_ROLL) {
        uint32_t ph = song.playing ? t->seq_idx : 0xFFFFu;
        if (pg->graph == GR_ROLL && ph / 16u != ui.bank)
            ph = 0xFFFFu;                            /* the roll shows the cursor's bank only */
        h ^= steps_hash(t) + ph * 31u + ui.cursor * 7919u;
    }
    return h;
}
/* preset browser: the list by kind (ui.c BANK), the current one in white */
static void graph_browse(void)
{
    uint32_t total, cur = preset_pos(&total), e, k;
    int32_t row;
    if (!total)
        return;
    for (row = -3; row <= 3; row++) {
        int32_t y = 4 + (row + 3) * 17;
        char tag[5], nm[13];
        int sel = row == 0;
        uint32_t n = (cur + total * 4u + (uint32_t)row) % total;
        e = preset_at(n, &k);
        if (e == NENGINES) {                             /* user preset: "U07" and its name */
            up_slot_label(tag, k);
            up_name(k, nm);
        } else {                                         /* its kind: BASS, KEYS, PAD... */
            str_cpy(tag, preset_kind(n), sizeof tag);
            str_cpy(nm, ENGINES[e]->presets[k].name, sizeof nm);
        }
        if (sel)
            cv_rect(4, y + 6, 3, 3, C_WHITE);
        cv_text(14, y, &FONT_S, tag, sel ? C_GRAY : C_DIM);
        cv_text(54, y, &FONT_S, nm, sel ? C_WHITE : C_GRAY);
    }
}

/* user preset slots around the selected one: "U07  NAME" / EMPTY, the selected one in white */
static void graph_user(void)
{
    int32_t row, first = clamp((int32_t)ui.uslot - 3, 0, UP_SLOTS - 7);
    for (row = 0; row < 7; row++) {
        uint32_t k = (uint32_t)(first + row);
        int32_t y = 4 + row * 17;
        char tag[4], nm[13];
        int sel = k == ui.uslot, used = up_used(k);
        up_slot_label(tag, k);
        if (used)
            up_name(k, nm);
        else
            str_cpy(nm, "EMPTY", sizeof nm);
        if (sel)
            cv_rect(4, y + 6, 3, 3, C_WHITE);
        cv_text(14, y, &FONT_S, tag, sel ? C_WHITE : C_GRAY);
        cv_text(54, y, &FONT_S, nm, used ? (sel ? C_WHITE : C_HI) : C_DIM);
    }
}

/* project slots: used / empty, the selected one in white */
static void graph_slots(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        int32_t y = 8 + (int32_t)i * 26;
        char b[4];
        int sel = (int32_t)i + 1 == song.g[G_SLOT];
        b[0] = (char)('1' + i);
        b[1] = 0;
        if (sel)
            cv_rect(4, y + 6, 3, 3, C_WHITE);
        cv_text(14, y, &FONT_S, b, sel ? C_WHITE : C_GRAY);
        cv_text(40, y, &FONT_S, project_used(i) ? "USED" : "EMPTY", project_used(i) ? (sel ? C_WHITE : C_HI) : C_DIM);
    }
}

static uint32_t trk_level(uint32_t c)                /* LEVEL 0..127 (the drum track: GLO > DRUMS LEVEL) */
{
    return (uint32_t)(c == TRK_DRUM ? song.g[G_DRLVL] : trk[c].p[P_LEVEL]) & 127u;
}

static void trk_short_name(uint32_t c, char *b)      /* the track's sound, b holds 13 */
{
    const track_t *t = &trk[c];
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    if (c == TRK_DRUM)
        str_cpy(b, "DRUM", 13);
    else if (user_of(t) < UP_SLOTS)
        up_name(user_of(t), b);
    else if (e->npresets)
        str_cpy(b, e->presets[t->preset % e->npresets].name, 13);
    else
        str_cpy(b, e->name, 13);
}

/* oscilloscope of the output across the canvas, centred on row cy, auto-scaled to amp rows, triggered on
 * a rising zero crossing (the wave stands still); one anti-aliased column a sample (cv_curve) */
static void graph_scope(uint16_t c, int32_t cy, int32_t amp)
{
    static int16_t snap[SCOPE_N];
    uint32_t w = scope_w, i, trig = 0;
    int32_t x, peak = 1500;
    for (i = 0; i < SCOPE_N; i++) {
        snap[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
        if (snap[i] > peak)
            peak = snap[i];
        else if (-snap[i] > peak)
            peak = -snap[i];
    }
    trig = 2;
    for (i = 3; i < SCOPE_N - 244u; i++)
        if (snap[i - 1] < 0 && snap[i] >= 0) {
            trig = i;
            break;
        }
    cv_rect(0, cy, 240, 1, C_LINE);
    cv_curve_begin(26, c);
    for (x = 0; x < 240; x++) {                      /* (a 5-sample mean a column: the shape, not the hiss) */
        const int16_t *q = &snap[trig + (uint32_t)x - 2u];
        int32_t v = (q[0] + q[1] + q[2] + q[3] + q[4]) / 5;
        cv_curve_pt(x, cy * 16 + 8 - v * amp * 16 / peak);
    }
    cv_curve_end();
}

/* A page without a graph (VOICE, the DEST pages, GLOBAL, MASTER, SYSTEM, the FX bus...), as the OP-1's
 * "02 34 45 23": the four knobs in a row, as they are, each a large dial (an amount; a plain ring for a
 * choice) with its label over it and its value large under it, the one turned in white; one value alone
 * (FILTER) large in the middle. No knob strip under it: this is the strip. The columns (draw_columns,
 * first) fill ui.big_* and col_ratio */
static void graph_big(void)
{
    uint32_t c, n = 0;
    for (c = 0; c < 4u; c++)
        n += ui.big_l[c][0] != 0;
    for (c = 0; c < 4u; c++) {
        int32_t cx = n == 1u ? 120 : 30 + 60 * (int32_t)c, r = n == 1u ? 34 : 22, vy = 20 + 2 * r + 10, x;
        uint16_t vc = ui.big_c[c] == C_DIM ? TE_G3 : ui.big_c[c] == C_WHITE ? C_WHITE : TE_COL[c & 3u];
        char l[16], v[16];
        if (!ui.big_l[c][0])
            continue;
        te_upfit(l, ui.big_l[c], sizeof l, n == 1u ? 200 : 58);
        te_text_c(cx, 0, l, TE_G3);
        te_dial(cx, 20 + r, r, col_ratio[c], ui.big_c[c] == C_DIM ? TE_G2 : TE_COL[c & 3u], TE_DIM[c & 3u]);
        te_upfit(v, ui.big_v[c], sizeof v, n == 1u ? 200 : 58);
        if (text_w(&FONT_L, v) + (ui.big_u[c][0] ? text_w(&FONT_S, ui.big_u[c]) + 3 : 0) <= (n == 1u ? 220 : 58)) {
            x = cx - (text_w(&FONT_L, v) + (ui.big_u[c][0] ? text_w(&FONT_S, ui.big_u[c]) + 3 : 0)) / 2;
            x = cv_text(x, vy, &FONT_L, v, vc);
            if (ui.big_u[c][0])
                cv_text(x + 3, vy + 10, &FONT_S, ui.big_u[c], TE_G3);
        } else {
            te_text_c(cx, vy + 8, v, vc);
        }
    }
}

/* the body (rows 24..183) of a page: its graph, the big dials, or what the drum track has here */
static void draw_graph(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint16_t c = TE_COL[song.sel & 3u];               /* LIVE: the curves in the track's colour */
    uint32_t drum_note = is_drum(t) && !page_for_drum(pg), big = !drum_note && big_page(pg), sig;
    sig = graph_signature() ^ (big * 0x9E37u + drum_note * 0x7F4Au);
    if (!ui.force && sig == ui.graph_sig)
        return;
    ui.graph_sig = sig;
    cv_begin(240, 124, C_BLACK);
    if (big) {                                       /* the big dials, centred in rows 24..239 */
        graph_big();
        cv_blit(0, 72);
        return;
    }
    cv_oy = 12;
    if (drum_note) {                                 /* a page the drum track has no use for */
        static const char *const L[2] = {"HOME, EDIT: DRUMS", "GLO: GLOBALS"};
        cv_text((240 - text_w(&FONT_S, L[0])) / 2, 26, &FONT_S, L[0], C_HI);
        cv_text((240 - text_w(&FONT_S, L[1])) / 2, 52, &FONT_S, L[1], C_DIM);
    } else {
        switch (pg->graph) {
        case GR_LFO:
            graph_lfo(t, c);
            break;
        case GR_STEPS:
            graph_steps(t, c);
            break;
        case GR_ROLL:
            graph_roll(t, c);
            break;
        case GR_SCALE:
            graph_scale(t, c);
            break;
        case GR_SLCR:
            graph_slicer(t, c);
            break;
        case GR_BROWSE:
            cv_oy = 0;
            graph_browse();
            break;
        case GR_SLOTS:
            cv_oy = 0;
            graph_slots();
            break;
        case GR_USER:
            cv_oy = 0;
            graph_user();
            break;
        case GR_MACROS:                              /* (the strip names the four; what is left: how to keep them) */
            te_text_c(120, 40, "PERFORM / USER: SAVE", C_DIM);
            break;
        default:
            break;
        }
    }
    cv_oy = 0;
    cv_blit(0, 40);
}

/* the knob strip of a page with a graph (a big-value page has its big dials instead): label and value
 * of each knob, no dials (the graph is the picture) */
static void gen_strip(void)
{
    static uint32_t cache;
    char v[4][20];
    const char *lab[4] = {ui.big_l[0], ui.big_l[1], ui.big_l[2], ui.big_l[3]}, *val[4] = {v[0], v[1], v[2], v[3]};
    static const int32_t ratio[4] = {NO_DIAL, NO_DIAL, NO_DIAL, NO_DIAL};
    uint32_t c;
    for (c = 0; c < 4u; c++) {
        str_cpy(v[c], ui.big_v[c], sizeof v[c]);
        if (ui.big_u[c][0] && ui.big_v[c][0]) {
            str_cpy(v[c] + str_len(v[c]), " ", 2);
            str_cpy(v[c] + str_len(v[c]), ui.big_u[c], sizeof v[c] - str_len(v[c]));
        }
    }
    te_dials(184, lab, val, ratio, ui.page * 31u + song.sel, &cache);
}

static void draw_columns(void)
{
    uint32_t c;
    char val[12];
    const char *unit;
    if (is_drum(TSEL) && !page_for_drum(cur_page())) {   /* "DRUM TRACK" (the graph says so) */
        for (c = 0; c < 4u; c++)
            draw_column(c, "", "", "", C_HI, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->graph == GR_BROWSE) {
        uint32_t total, cur = preset_pos(&total);
        char u[8];
        fmt_int(val, (int32_t)cur + 1);
        str_cpy(u, "/", 8);
        fmt_int(u + 1, (int32_t)total);
        draw_column(0, "No.", val, u, VAL(0u), -1, ICON_NONE);
        draw_column(1, "ENG", ENGINES[TSEL->eng_req]->name, "", VAL(1u), -1, engine_icon(ENGINES[TSEL->eng_req]->name));
        draw_column(2, "", "", "", C_HI, -1, ICON_AUTO);
        draw_column(3, "", "", "", C_HI, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->graph == GR_USER) {                  /* SLOT, then three GO buttons */
        int used = up_used(ui.uslot);
        up_slot_label(val, ui.uslot);
        draw_column(0, "SLOT", val, "", VAL(0u), (int32_t)ui.uslot * 1000 / (int32_t)(UP_SLOTS - 1u), ICON_AUTO);
        draw_column(1, "LOAD", "--", "", used ? C_HI : C_DIM, -1, ICON_AUTO);
        draw_column(2, "ERASE", "--", "", used ? C_HI : C_DIM, -1, ICON_AUTO);
        draw_column(3, "SAVE", "--", "", C_HI, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->graph == GR_MACROS) {
        static const char *const LABEL[4] = {"KNOB1", "KNOB2", "KNOB3", "KNOB4"};
        for (c = 0; c < MACRO_COUNT; c++) {
            uint32_t id = macro_id(TSEL, c);
            draw_column(c, LABEL[c], macro_eligible(TSEL, id) ? track_desc(TSEL, id)->label : "--",
                        "", VAL(c), -1, ICON_NONE);
        }
        return;
    }
    if (cur_page()->scope == SC_STEP) {
        static const char *const TIME_N[3] = {"NOTE", "TIE", "REST"};
        const step_t *st = &TSEL->step[ui.cursor];
        char u[8];
        if (st->n) {
            note_name(val, st->note[0]);
            u[0] = 0;
            if (st->n > 1) {
                str_cpy(u, "+", 8);
                fmt_int(u + 1, st->n - 1);
            }
        } else {
            str_cpy(val, "--", 12);
            u[0] = 0;
        }
        {
            static const char *const FLAG_N[4] = {"-", "ACC", "SLD", "A+S"};
            char sn[8], sl[8];
            fmt_int(sn, (int32_t)ui.cursor + 1);
            str_cpy(sl, "/", 8);
            fmt_int(sl + 1, TSEL->p[P_SLEN]);
            draw_column(0, "STEP", sn, sl, VAL(0u), -1, ICON_AUTO);
            draw_column(1, "NOTE", val, u, step_on(st) ? VAL(1u) : C_DIM, -1, ICON_AUTO);
            if (step_on(st)) {                          /* a note: its LEN (2.6), steps with the TIEs after it */
                char ln[8];
                fmt_int(ln, (int32_t)note_len(TSEL, ui.cursor));
                draw_column(2, "LEN", ln, "STEP", VAL(2u), -1, ICON_AUTO);
            } else {
                draw_column(2, "TIME", TIME_N[st->time % 3u], "", VAL(2u), -1, ICON_AUTO);
            }
            draw_column(3, "FLAG", FLAG_N[(st->flags & SF_ACCENT ? 1u : 0u) | (st->flags & SF_SLIDE ? 2u : 0u)], "",
                        VAL(3u), -1, ICON_AUTO);
        }
        return;
    }
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(cur_page(), c, &vp);
        if (cur_page()->graph == GR_SLOTS && c == 1u) {   /* 2.6: ERASE, lit while the slot holds a project */
            draw_column(1, "ERASE", "--", "", project_used((uint32_t)song.g[G_SLOT] - 1u) ? C_HI : C_DIM, -1, ICON_AUTO);
            continue;
        }
        if (!d || !d->label || d->label[0] == '-') {
            draw_column(c, "", "", "", C_HI, -1, ICON_AUTO);
            continue;
        }
        if (cur_page()->id[c] == G_INFO && cur_page()->scope == SC_GLOBAL && !usb.config) {   /* not connected: the
                                                       * USB status instead of the CPU (2.4: MIDI is MIDI OUT, ROUT is IN) */
            str_cpy(val, !usb.up ? "OFF" : usb.config ? "MIDI" : usb.setups ? "ENUM" : usb.sof_seen ? "BUS" : "WAIT", 12);
            unit = "USB";
            draw_column(c, "USB", val, unit, C_HI, -1, ICON_AUTO);
            continue;
        }
        if (cur_page()->id[c] == G_INFO && cur_page()->scope == SC_GLOBAL) {
            fmt_int(val, (int32_t)(song.cpu_q8 * 100u / 256u));
            unit = "%";
        } else {
            param_format(d, *vp, val, &unit);
        }
        draw_column(c, d->label, val, unit, VAL(c), d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, *vp),
                    param_icon(d, *vp));
    }
}


/* the UI's timers, once a frame whatever the screen (a message, the BPM highlight, an armed GO, the
 * white value of the knob just turned) */
static void ui_timers(void)
{
    if (ui.msg_t)
        ui.msg_t--;
    if (ui.hint_t)
        ui.hint_t--;
    if (ui.bpm_t)
        ui.bpm_t--;
    if (ui.arm_t && !--ui.arm_t)
        ui.arm = 0;
    if (ui.hot_t)
        ui.hot_t--;
    if (ui.pl_t)                                     /* the PRESETS list: expires, or closes on navigation */
        ui.pl_t = pl_shown() ? (uint8_t)(ui.pl_t - 1u) : 0u;
}


#include "orbit_modes.c"
static void vis_draw(void);                     /* ui_vis.c */
static int vis_shown(void);
static uint8_t vis_on, vis_shown_last, vis_name_t;   /* the visualiser (ui_vis.c): on, drawn last frame, its name's time */
static void ui_draw(void)
{
    ui.frame++;
    pads_tick();
    if (rec_go) {                                       /* the take started: say so */
        rec_go = 0;
        ui_message("RECORDING");
    }
    if (ft_bars) {                                      /* a free take closed: the loop it made */
        char m[24];
        uint32_t n = ft_bars;
        ft_bars = 0;
        if (n == 0xFFu) {
            ui_message("TAKE DROPPED");
        } else {
            str_cpy(m, n == 1u ? "LOOP 1 BAR " : n == 2u ? "LOOP 2 BARS " : "LOOP 4 BARS ", sizeof m);
            fmt_int(m + str_len(m), song.g[G_BPM]);
            str_cpy(m + str_len(m), " BPM", sizeof m - str_len(m));
            ui_message(m);
        }
    }
    if (er_flash) {                                     /* EDIT + key took something out */
        er_flash = 0;
        if (!ui.msg_t)
            ui_message("ERASED");
    }
    if (!ui.menu && (ui.layer != LY_PLAY || ui.hold_kind)) {   /* a layer held / a hold to confirm */
        if (ui.hold_kind)
            hold_screen_draw();
        else
            layer_screen_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (layer_shown) {                                  /* back to the page */
        layer_shown = 0;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    if ((rec_wait || ft_on) && !ui.menu && !on_song_page()) {   /* armed / free take */
        rec_screen_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (rec_shown) {                                    /* back to the page */
        rec_shown = 0;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
#if FELUCCA_ARRANGER
    if (!ui.menu && on_song_page()) {
        song_screen_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
#endif
    if (ui.menu) {
        draw_menu();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (vis_shown()) {                                  /* the visualiser over the TRACKS screen */
        vis_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (vis_shown_last) {                               /* back from it */
        vis_shown_last = 0;
        vis_on = 0;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    if (ui.home) {
        orbit_screen_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (!ui.home && cur_page()->scope == SC_TRK) {
        orbit_mixer_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (on_drum_page()) {
        drum_screen_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (orbit_sound_page()) {
        orbit_sound_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    cursor_fix();
    if (ui.force)
        lcd_fill(0, 24, 240, 216, C_BLACK);
    felucca_dbg.stage = 3;
    {   /* the header: the page and where it is in its family; MIDI LEARN, the octave when not 0 */
        static uint32_t head;
        const page_t *pg = cur_page();
        const char *pt = pg->scope == SC_ENGINE ? ENGINES[TSEL->eng_req % NENGINES]->page_title[pg->id[0] != P_E0] : 0;
        char sub[24];
        page_pos(sub);
        if (mlu.on)
            str_cpy(sub, "MIDI LEARN", sizeof sub);
        else if (song.octave) {
            str_cpy(sub + str_len(sub), sub[0] ? "  OCT " : "OCT ", 8);
            str_cpy(sub + str_len(sub), song.octave > 0 ? "+" : "", 2);
            fmt_int(sub + str_len(sub), song.octave);
        }
        ui_head(pt ? pt : pg->title, C_WHITE, sub, 0, &head);
    }
    felucca_dbg.stage = 4;
    draw_columns();
    felucca_dbg.stage = 5;
    draw_graph();
    ui_timers();
    felucca_dbg.stage = 6;
    if (!big_page(cur_page()) || (is_drum(TSEL) && !page_for_drum(cur_page())))
        gen_strip();
    ui.force = 0;
}


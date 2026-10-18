/* SPDX-License-Identifier: GPL-3.0-only */
/* LIVE screens, 240 x 240, in a teenage-engineering-like style: black, four colours (one per
 * track and per knob: blue, green, white, orange), white for what you touch, red for recording,
 * big numbers, lowercase labels, four dials at the bottom that show what KNOB 1..4 do.
 * Screens: TRACKS (the performance view), DRUMS (GRID / KIT, pads that flash on each hit), the
 * LAYERS (a function button held: what the 16 white keys and the knobs do now), HOLD (a hold to
 * confirm: clear, save), REC (armed / free take). Every band is drawn into the canvas only when its
 * signature changed. */
static uint8_t drum_page, drum_lane, drum_cursor;   /* drum_page: 0 GRID, 1 KIT */
static const uint8_t GRID_LANE[11] = {0, 2, 4, 5, 3, 7, 9, 10, 11, 12, 15};
static void trk_short_name(uint32_t c, char *b);
static int32_t batt_shown(void);
static void draw_battery(int usb_label);
static int on_drum_page(void) { return !ui.home && cur_page()->scope == SC_DRUM; }

/* ---------------------------------------------------------------- style --- */
#define TE_G1 te_g[0]                    /* tiles (gfx.c palette_set: lavender with ORBIT) */
#define TE_G2 te_g[1]                    /* empty steps, dial tracks, tile outlines */
#define TE_G3 te_g[2]                    /* labels */
#define TE_G4 te_g[3]                    /* secondary text */
#define TE_RED te_alert          /* recording, erasing */
#define TE_DRUM TE_COL[3]

static void te_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)     /* filled circle */
{
#if CV_AA
    cv_disc_q4(cx * 16 + 8, cy * 16 + 8, r * 16 + 8, c);
#else
    int32_t y, x;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                cv_pset(cx + x, cy + y, c);
#endif
}
/* a dial, as the OP-1's encoders: a hairline 270-degree track, the value lit along it in the colour,
 * a fine white needle; ratio 0..1000, -1 = a plain ring */
static void te_dial(int32_t cx, int32_t cy, int32_t r, int32_t ratio, uint16_t c, uint16_t dim)
{
    int32_t end = ratio < 0 ? 768 : ratio * 768 / 1000;
#if CV_AA
    int32_t ccx = cx * 16 + 8, ccy = cy * 16 + 8, rad = r * 16 - 12;
    (void)dim;
    cv_arc_q4(ccx, ccy, rad, 14, 384u, 768u, ratio < 0 ? c : TE_G2);
    if (ratio >= 0 && end > 0)
        cv_arc_q4(ccx, ccy, rad, 30, 384u, (uint32_t)end, c);
    if (ratio >= 0) {
        uint32_t a = (uint32_t)(384 + end) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a];
        cv_line_q4(ccx, ccy, ccx + ((co * (r - 5) * 16) >> 15), ccy + ((si * (r - 5) * 16) >> 15), 18, C_WHITE);
        cv_disc_q4(ccx, ccy, 28, C_WHITE);
    }
#else
    int32_t i;
    for (i = 0; i <= 768; i += 8) {
        uint32_t a = (uint32_t)(384 + i) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a], k;
        uint16_t col = ratio < 0 || i <= end ? c : dim;
        for (k = r - 2; k <= r; k++)
            cv_pset(cx + ((co * k) >> 15), cy + ((si * k) >> 15), col);
    }
    if (ratio >= 0) {
        uint32_t a = (uint32_t)(384 + end) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a];
        cv_line(cx, cy, cx + ((co * (r - 4)) >> 15), cy + ((si * (r - 4)) >> 15), C_WHITE);
        te_disc(cx, cy, 2, C_WHITE);
    }
#endif
}
static void te_play_icon(int32_t x, int32_t y, int playing)
{
    int32_t i;
    if (playing)
        for (i = 0; i < 12; i++)
            cv_rect(x + i, y + i / 2, 1, 14 - i, C_WHITE);   /* a triangle */
    else
        cv_rect(x, y + 1, 12, 12, TE_G3);
}
static uint32_t studio_hash(uint32_t h, const char *p)
{ while (*p) h = h * 31u + (uint8_t)*p++; return h; }
static void te_lower(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        d[i] = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] + 32) : s[i];
    d[i] = 0;
}
static void te_text_c(int32_t cx, int32_t y, const char *s, uint16_t c)   /* centred on cx */
{
    cv_text(cx - text_w(&FONT_S, s) / 2, y, &FONT_S, s, c);
}
/* d = s in capitals, cut (by width, not letters) to maxw pixels of the small font */
static void te_upfit(char *d, const char *s, uint32_t n, int32_t maxw)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        d[i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];
    d[i] = 0;
    while (d[0] && text_w(&FONT_S, d) > maxw)
        d[str_len(d) - 1u] = 0;
}
/* a box as the OP-1 draws them: an outline in a quiet colour, or filled (lit) with its corners
 * taken off */
static void te_box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, int filled)
{
    if (filled) {
        cv_rect(x + 1, y, w - 2, h, c);
        cv_rect(x, y + 1, 1, h - 2, c);
        cv_rect(x + w - 1, y + 1, 1, h - 2, c);
        return;
    }
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y + 1, 1, h - 2, c);
    cv_rect(x + w - 1, y + 1, 1, h - 2, c);
}

/* the knob strip, KNOB 1..4 in a row, as the knobs are: a small dial, its label, the value large and thin
 * in the knob's colour (white while turned); a value too wide for the large type is small. The dial is
 * drawn only where it is the one picture of a continuous value: ratio NO_DIAL (an enum, an action, or a
 * value the screen already draws) gives the label and value alone; an empty label and value, a knob that
 * does nothing here, an empty column. Messages are the header's (ui_head) */
static void te_dials(int32_t y0, const char *const lab[4], const char *const val[4], const int32_t ratio[4],
                     uint32_t sig, uint32_t *cache)
{
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        sig = studio_hash(sig * 7u + (uint32_t)ratio[k] + (ui.hot_t && ui.hot_col == k) * 5003u, lab[k]);
        sig = studio_hash(sig, val[k]);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 240u - (uint32_t)y0, C_BLACK);
    for (k = 0; k < 4u; k++) {
        int32_t cx = 30 + 60 * (int32_t)k;
        uint16_t vc = ui.hot_t && ui.hot_col == k ? C_WHITE : TE_COL[k];
        char l[12];
        if (lab[k][0]) {
            if (ratio[k] >= -1)
                te_dial(cx, 9, 9, ratio[k], TE_COL[k], TE_DIM[k]);
            te_upfit(l, lab[k], sizeof l, 58);
            te_text_c(cx, 15, l, TE_G3);
        }
        te_upfit(l, val[k], sizeof l, 58);
        if (text_w(&FONT_L, l) <= 58)
            cv_text(cx - text_w(&FONT_L, l) / 2, 24, &FONT_L, l, vc);
        else
            te_text_c(cx, 32, l, vc);
    }
    cv_blit(0, (uint32_t)y0);
}

static void studio_open(uint32_t scope)
{
    uint32_t i;
    if (scope == SC_SONG && rec_wait)
        rec_wait = 0;                                   /* (an arm does not follow into the song page) */
    for (i = 0; i < NPAGES; i++) if (PAGES[i].scope == scope) {
        ui.page = (uint8_t)i; ui.home = 0; song.seq_mode = 0; ui.force = 1;
        ui.msg_t = 0; ui.entry_open = 0; return;
    }
}
/* LIVE: REC, on any page. One record arm, on the selected track (it follows ALGORITHM). No
 * click (seq.c): playing, recording starts at once; stopped, REC arms and the first note
 * starts the loop (or, in an empty project, a free take that REC closes: seq.c ft_close).
 * REC again stops recording / cancels the arm (playback continues). */
static void rec_toggle(void)
{
    if (ft_owns_press())
        return;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    if (song.rec || rec_wait) {
        song.rec = 0;
        rec_wait = 0;
        ui_message("REC OFF");
        return;
    }
    arrangement_enabled = 0;
    if (song.playing)
        rec_begin();                    /* playing: record now */
    else
        rec_wait = 1;                   /* stopped: the first note starts it */
}

/* The one header (rows 0..23), every screen: the title in its colour; after it the BPM (HEAD_BPM, the
 * screens you play on, or a second after SELECT turned it), then the screen's state (sub) or, its first
 * two seconds, its key hint (ui_hint); a message replaces them (not with HEAD_NOMSG). Right: a red dot
 * while recording or armed, else a white triangle while playing; the selected track (a chip in its
 * colour); the battery. */
static const char *head_title_shown = "";               /* (the host tests read it) */
static void ui_head(const char *title, uint16_t tc, const char *sub, uint32_t flags, uint32_t *cache)
{
    static const char *hint_for;                        /* the screen (its title) the hint is for */
    char t[28], b[8];
    const char *suffix;
    if (ui.hint_t && ui.hint_new) {
        hint_for = title;
        ui.hint_new = 0;
    } else if (ui.hint_t && hint_for != title) {
        ui.hint_t = 0;                                  /* (another screen's hint) */
    }
    suffix = ui.hint_t && ui.hint ? ui.hint : sub;
    uint32_t playing = song.playing, rec = song.rec != 0u || rec_wait || ft_on, msg = ui.msg_t && !(flags & HEAD_NOMSG);
    uint32_t bpm = (flags & HEAD_BPM) || ui.bpm_t, sig;
    int32_t x, i;
    sig = studio_hash(studio_hash(studio_hash(tc * 7u + playing + rec * 3u + song.sel * 11u + bpm * 13u +
                                              (ui.bpm_t != 0) * 17u + (uint32_t)song.g[G_BPM] * 19u +
                                              (uint32_t)batt_shown() * 23u, title),
                                  msg ? ui.msg : ""), suffix ? suffix : "");
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    head_title_shown = title;
    cv_begin(240, 24, C_BLACK);
    if (msg) {
        te_upfit(t, ui.msg, sizeof t, 178);
        cv_text(6, 3, &FONT_S, t, C_WHITE);
    } else {
        te_upfit(t, title, sizeof t, 120);
        x = cv_text(6, 3, &FONT_S, t, tc);
        if (bpm) {
            fmt_int(b, song.g[G_BPM]);
            x = cv_text(x + 10, 3, &FONT_S, b, ui.bpm_t ? C_WHITE : TE_G4);
            x = cv_text(x + 3, 3, &FONT_S, "BPM", TE_G3);
        }
        if (suffix && suffix[0]) {
            te_upfit(t, suffix, sizeof t, 182 - (x + 10));
            cv_text(x + 10, 3, &FONT_S, t, TE_G3);
        }
    }
    if (rec)
        te_disc(191, 11, 4, TE_RED);
    else if (playing)
        for (i = 0; i < 8; i++)                         /* a small triangle */
            cv_rect(187 + i, 7 + i / 2, 1, 9 - (i / 2) * 2, C_WHITE);
    te_box(200, 4, 13, 15, TE_COL[song.sel & 3u], 1);
    b[0] = (char)('1' + song.sel);
    b[1] = 0;
    cv_text(206 - text_w(&FONT_S, b) / 2, 2, &FONT_S, b, C_BLACK);
    draw_battery(0);
    cv_blit(0, 0);
}

/* the page's place in its family as "2/3" (the pages the track has), or "" when the family is one page */
static void page_pos(char *b)
{
    uint32_t i, n = 0, k = 0;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == cur_page()->fam && (page_shown(&PAGES[i]) || i == ui.page)) {
            n++;
            if (i == ui.page)
                k = n;
        }
    b[0] = 0;
    if (n > 1u) {
        fmt_int(b, (int32_t)k);
        str_cpy(b + str_len(b), "/", 2);
        fmt_int(b + str_len(b), (int32_t)n);
    }
}

/* --------------------------------------------------------------- TRACKS --- */
/* swing: 0 straight .. 100 the most (params.c F_SWING) */
static void swing_str(char *b, int32_t v)
{
    fmt_int(b, clamp(v, 0, 100));
}

/* ---------------------------------------------------------------- DRUMS --- */
static const char *const LV_NAME[4] = {"norm", "ghost", "soft", "hard"};
static uint32_t lvl_rank(uint32_t lvl);                 /* (below) ghost 0, soft 1, norm 2, hard 3 */
static uint16_t lvl_col(uint32_t f, uint32_t lvl)       /* a hit's colour by its level, in its family's colour f */
{
    return lvl == LV_GHOST || lvl == LV_SOFT ? TE_MID[f & 3u] : TE_COL[f & 3u];   /* (GHOST, SOFT: their size tells) */
}
static uint16_t pad_lit[DRUM_LANES];                   /* pads and key LEDs: frames left lit */
static uint8_t key_lit[27];                             /* menu NOTES, a synth track: frames its key stays lit */
static uint8_t key_lit_trk = 0xFF;                      /* (the track key_lit is for) */
static void pads_tick(void)                             /* once a frame: the hits since the last one */
{
    uint32_t i, hits, n[NPART][4], k, w;
    fm1_irq_off();
    hits = drums.hits;
    drums.hits = 0;
    for (i = 0; i < NPART; i++)
        for (w = 0; w < 4u; w++) {
            n[i][w] = note_hits[i][w];
            note_hits[i][w] = 0;
        }
    fm1_irq_on();
    for (i = 0; i < DRUM_LANES; i++) {
        if ((hits >> i) & 1u) pad_lit[i] = 6;
        else if (pad_lit[i]) pad_lit[i]--;
    }
    /* a synth track: each note it started lights its key 6 frames (~0.1 s), so a short sequencer note
     * (a 1/16 at GATE 50 %: 60..80 ms) is seen even when it ends between two frames */
    if (song.sel != key_lit_trk) {
        memset(key_lit, 0, sizeof key_lit);
        key_lit_trk = song.sel;
    }
    for (k = 0; k < 27u; k++)
        if (key_lit[k])
            key_lit[k]--;
    if (song.sel < NPART && (n[song.sel][0] | n[song.sel][1] | n[song.sel][2] | n[song.sel][3]))
        for (k = 0; k < 27u; k++) {
            uint32_t note = kb_map(TSEL, k);
            if (note < 128u && (n[song.sel][note >> 5] >> (note & 31u)) & 1u)
                key_lit[k] = 6;
        }
}

/* Compact row labels (capitals and digits), centred in a tile w wide: the small font's cap
 * height (9 px) is the row, so the cell is drawn from the top of 'H' (its first inked row). */
static void drum_row_text(int32_t x, int32_t w, int32_t y, const char *s, uint16_t c)
{
    cv_text(x + (w - text_w(&FONT_S, s)) / 2, y - (int32_t)FONT_S.data[FONT_S.off[glyph(&FONT_S, 'H')]], &FONT_S, s, c);
}
static const char GRID_LABEL[DRUM_LANES][3] = {
    "BD", "B2", "SN", "CP", "CH", "OH", "PD", "RS",
    "S2", "LT", "HT", "CR", "RD", "SH", "CG", "CB"
};
/* GRID: each sound in its family's colour, the OP-1's four: kicks blue, snares and claps green, hats and
 * cymbals white, toms and percussion red; a groove reads at a glance */
static const uint8_t DRUM_FAM[DRUM_LANES] = {0, 0, 1, 1, 2, 2, 2, 1, 1, 3, 3, 2, 2, 2, 3, 3};
/* a hit at x, y (11 x 8): a dot, as large as it is loud (GHOST 3.5 px across .. HARD 9), its ratchets as many
 * dots side by side; white as the playhead plays it */
static void drum_cell(int32_t x, int32_t y, uint32_t f, uint32_t lvl, uint32_t rat, int playing)
{
    uint16_t c = playing ? C_WHITE : lvl_col(f, lvl);
    int32_t rad = lvl == LV_GHOST ? 28 : lvl == LV_SOFT ? 40 : lvl == LV_NORM ? 54 : 72;   /* 1/16 px: 3.5 .. 9 px across */
    int32_t i, n = rat ? (int32_t)rat : 1, room = 11 * 16 / (2 * n) - 6;
    if (rad > room)
        rad = room;                                 /* x2..x4: as many dots side by side */
    for (i = 0; i < n; i++)
        cv_disc_q4(x * 16 + (2 * i + 1) * 11 * 16 / (2 * n), (y + 4) * 16, rad, c);
}
static void drum_screen_draw(void)
{
    static uint32_t head, title_sig, body_sig, footer;
    uint32_t i, len = (uint32_t)clamp(TDRUM->p[P_SLEN], 1, 64), kit = drum_kit(), sig, bank;
    if (drum_cursor >= len) drum_cursor = (uint8_t)(len - 1u);
    bank = drum_cursor / 16u;
    if (ui.force && drum_page)
        lcd_fill(0, 24, 240, 160, C_BLACK);             /* (the bands below leave no gap of the other page) */
    {   /* GRID: the kit, the bank in view (the lane is the lit row); KIT: the kit large, its style */
        char b[8];
        b[0] = 0;
        if (!drum_page && len > 16u) {
            fmt_int(b, bank + 1u); b[1] = '/'; fmt_int(b + 2, (len + 15u) / 16u);
        }
        ui_head(drum_page ? "KIT" : DRUM_KIT_NAMES[kit], TE_DRUM, b, HEAD_BPM, &head);
    }
    sig = kit * 131u + drum_page * 7u;
    if (drum_page && (ui.force || sig != title_sig)) {
        char st[16], nm[9];
        const char *style = DRUM_KIT_STYLES[kit];
        title_sig = sig;
        if (kit >= DRUM_SYN) {                          /* SYN1..4: names from the editor's DRUM SYNTH page */
            memcpy(nm, dsu_kit(kit - DRUM_SYN)->name, 8);
            nm[8] = 0;
            if (nm[0])
                style = nm;
        }
        te_upfit(st, style, sizeof st, 100);
        cv_begin(240, 48, C_BLACK);
        cv_text(cv_text(8, 4, &FONT_L, DRUM_KIT_NAMES[kit], C_WHITE) + 12, 20, &FONT_S, st, TE_G3);
        cv_blit(0, 24);
    } else if (!drum_page)
        title_sig = ~sig;
    sig = drum_page + drum_lane * 7u + drum_cursor * 101u + song.playing * 71u + len * 3u;
    if (song.playing) sig = sig * 31u + TDRUM->seq_idx;
    for (i = 0; i < DRUM_LANES; i++) sig = sig * 3u + (pad_lit[i] != 0);
    for (i = 0; i < len; i++) {
        const dstep_t *s = &TDRUM->dstep[i];
        sig = sig * 31u + dstep_mask(s);
        sig = sig * 31u + s->lvl[0] + s->lvl[1] * 7u + s->lvl[2] * 49u + s->lvl[3] * 343u;
        sig = sig * 31u + s->rat[0] + s->rat[1] * 7u + s->rat[2] * 49u + s->rat[3] * 343u;
    }
    if (ui.force || sig != body_sig) {
        body_sig = sig;
        if (!drum_page) {                              /* GRID: instruments down, sixteen time columns across */
            uint32_t pass, j;
            int32_t ph = song.playing && TDRUM->seq_idx / 16u == bank ? (int32_t)(TDRUM->seq_idx % 16u) : -1;
            for (pass = 0; pass < 2u; pass++) {
                cv_begin(240, 80, C_BLACK);
                cv_oy = -(int32_t)(pass * 80u);
                if (ph >= 0)                           /* the playhead: its column lit */
                    cv_rect(23 + ph * 13, 0, 13, 160, TE_G1);
                for (i = 0; i < DRUM_LANES; i++) {
                    int32_t y = (int32_t)i * 10;
                    uint32_t f = DRUM_FAM[i];
                    if (i == drum_lane) {              /* the lane the white keys write: a band, its name a chip */
                        cv_rect(22, y - 1, 210, 10, TE_G1);
                        if (ph >= 0)
                            cv_rect(23 + ph * 13, y - 1, 13, 10, TE_G2);
                        cv_rect(0, y - 1, 20, 10, TE_COL[f]);
                    }
                    drum_row_text(0, 20, y, GRID_LABEL[i], i == drum_lane ? C_BLACK : pad_lit[i] ? TE_COL[f] : TE_G3);
                    for (j = 0; j < 16u; j++) {
                        uint32_t p = bank * 16u + j;
                        const dstep_t *s = &TDRUM->dstep[p];
                        int32_t x = 24 + (int32_t)j * 13;
                        if (p >= len) continue;
                        if (dstep_has(s, i))
                            drum_cell(x, y, f, dstep_lvl(s, i), dstep_rat(s, i), (int32_t)j == ph);
                        else                           /* an empty step: a dot, brighter on the beat and in the lane */
                            cv_rect(x + 5, y + 3, 1, 1, i == drum_lane || !(j & 3u) ? TE_G3 : TE_G2);
                        if (p == drum_cursor && i == drum_lane)
                            te_box(x - 1, y - 1, 13, 10, C_WHITE, 0);
                    }
                }
                cv_oy = 0;
                cv_blit(0, 24u + pass * 80u);
            }
        } else {                                       /* KIT: 16 pads, lit on each hit */
            cv_begin(240, 108, C_BLACK);
            for (i = 0; i < DRUM_LANES; i++) {
                int32_t x = 2 + (int32_t)(i % 4u) * 60, y = (int32_t)(i / 4u) * 27;
                te_box(x, y, 56, 25, pad_lit[i] ? TE_DRUM : i == drum_lane ? TE_G3 : TE_G2, pad_lit[i] != 0);
                te_text_c(x + 28, y + 5, LANE_SHORT[i], pad_lit[i] ? C_BLACK : i == drum_lane ? C_WHITE : TE_G3);
            }
            cv_blit(0, 74);
        }
    }
    {
        static char v[4][12];
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        static const char *const LG[4] = {"sound", "step", "hit", "level"}, *const LK[4] = {"kit", "level", "reverb", "pan"};
        int32_t ratio[4];
        if (!drum_page) {
            const dstep_t *s = &TDRUM->dstep[drum_cursor];
            str_cpy(v[0], LANE_SHORT[drum_lane], 8);
            fmt_int(v[1], drum_cursor + 1);
            str_cpy(v[2], dstep_has(s, drum_lane) ? "on" : "--", 4);
            str_cpy(v[3], dstep_has(s, drum_lane) ? LV_NAME[dstep_lvl(s, drum_lane)] : "--", 8);
            ratio[0] = ratio[1] = ratio[2] = ratio[3] = NO_DIAL;   /* the lit row, the cursor, the cell show them */
            te_dials(184, LG, val, ratio, 1u, &footer);
        } else {
            fmt_int(v[0], (int32_t)kit + 1);
            fmt_int(v[1], song.g[G_DRLVL] * 100 / 127);
            fmt_int(v[2], song.g[G_DRREV] * 100 / 127);
            fmt_int(v[3], TDRUM->p[P_PAN]);
            ratio[0] = NO_DIAL;                         /* (a kit is a name, not an amount) */
            ratio[1] = song.g[G_DRLVL] * 1000 / 127;
            ratio[2] = song.g[G_DRREV] * 1000 / 127;
            ratio[3] = (TDRUM->p[P_PAN] + 64) * 1000 / 127;
            te_dials(184, LK, val, ratio, 2u, &footer);
        }
    }
}

/* the level order of the knobs: ghost < soft < norm < hard */
static const uint8_t LV_UP[4] = {LV_GHOST, LV_SOFT, LV_NORM, LV_HARD};
static uint32_t lvl_rank(uint32_t lvl) { return lvl == LV_GHOST ? 0u : lvl == LV_SOFT ? 1u : lvl == LV_NORM ? 2u : 3u; }

/* the GRID page shown, no layer held: its keys are steps (seq.c kb_grid) */
static int grid_keys_on(void)
{
    return on_drum_page() && !drum_page && song.sel == TRK_DRUM && !ui.menu && !rec_wait && !ft_on && !ui.hold_kind;
}
static void drum_clear_empty(uint32_t step)
{
    if (!dstep_mask(&TDRUM->dstep[step])) {
        lock_del(TDRUM, step, P_COUNT);
        TDRUM->micro[step] = 0;
        step_fill_set(TDRUM, step, FC_NORM);
    }
}
/* GRID: black keys select voices silently; white keys toggle the current bank's steps.
 * KNOB 1 still reaches all 16 lanes; OCT changes banks, KNOB 2 moves the cursor. */
static void grid_key(uint32_t k)
{
    uint32_t len = trk_len(TDRUM), idx;
    int32_t w = punch_key(k);
    dstep_t *st;
    if (w < 0) {
        for (idx = 0; idx < 11u; idx++)
            if (PUNCH_BLACK[idx] == k) {
                drum_lane = GRID_LANE[idx];
                ui.force = 1;
                break;
            }
        return;
    }
    idx = drum_cursor / 16u * 16u + (uint32_t)w;
    if (idx >= len)
        return;
    drum_cursor = (uint8_t)idx;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    undo_mark(TDRUM, ui.step_sess ? ui.step_sess : (ui.step_sess = (undo_sess += 4u) | 3u));
    st = &TDRUM->dstep[idx];
    fm1_irq_off();
    if (dstep_has(st, drum_lane)) {
        dstep_clr(st, drum_lane);
        drum_clear_empty(idx);
    } else {
        dstep_set(st, drum_lane, LV_NORM, 0);
        TDRUM->seq_active = 1;
    }
    fm1_irq_on();
    if (dstep_has(st, drum_lane))
        audition_lane(drum_lane);
    sync_reload = 1;
}

static void drum_screen_input(uint32_t pressed, uint32_t home)
{
    uint32_t k, b;
    int32_t s;
    if (song.sel != TRK_DRUM || home == 1u) {
        go_home();
        return;
    }
    for (k = 0; k < NB; k++) if ((pressed >> panel.btn[k]) & 1u) {
        b = k;
        if (b == B_PLAY) {
            if (ft_owns_press()) ;
            else if (!song.playing && arrangement_enabled && !arr_valid(&arrangement, arrangement_ready())) ui_message("EMPTY SECTION: REC");
            else transport_req = song.playing ? 2 : 1;
        } else if (b == B_SEQ || b == B_EDIT) {
            drum_page = (uint8_t)((drum_page + 1u) % 2u);
            ui.msg_t = 0;
            ui.force = 1;
        } else if (b == B_SAVE) {
            studio_open(SC_SONG);
            return;
        } else if (!drum_page && (b == B_OCTDN || b == B_OCTUP)) {
            uint32_t len = trk_len(TDRUM), pages = (len + 15u) / 16u;
            uint32_t bank = (drum_cursor / 16u + (b == B_OCTUP ? 1u : pages - 1u)) % pages;
            uint32_t idx = bank * 16u + drum_cursor % 16u;
            drum_cursor = (uint8_t)(idx < len ? idx : len - 1u);
            ui.force = 1;
        }
    }
    if ((s = panel_enc(EN_SELECT)) && (uint32_t)(s > 0) != drum_page) {   /* SELECT: grid <- -> kit (its pages) */
        drum_page = (uint8_t)(s > 0);
        ui.msg_t = 0;
        ui.force = 1;
    }
    if ((s = panel_enc(EN_ALGO)) && !ft_on) {
        track_select((uint32_t)clamp((int32_t)song.sel + s, 0, 3));
        if (song.sel != TRK_DRUM) {
            go_home();
            return;
        }
    }
    if ((s = panel_enc(EN_PRESET))) TDRUM->p[P_E0] = (int16_t)clamp(TDRUM->p[P_E0] + s, 0, DRUM_KITS - 1);
    for (k = 0; k < 4u; k++) if ((s = panel_enc(EN_K1 + k))) {
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        if (!drum_page) {
            dstep_t *st = &TDRUM->dstep[drum_cursor];
            if (k == 0) {                              /* the sound: heard */
                uint8_t l = (uint8_t)clamp(drum_lane + s, 0, DRUM_LANES - 1);
                if (l != drum_lane) audition_lane(l);
                drum_lane = l;
            }
            if (k == 1) {                              /* the step: what it holds, heard */
                uint8_t c = (uint8_t)clamp(drum_cursor + s, 0, TDRUM->p[P_SLEN] - 1);
                if (c != drum_cursor) audition_step(&TDRUM->dstep[c]);
                drum_cursor = c;
            }
            if (k >= 2) {
                if (song.playing && arrangement_enabled) { ui_message("STOP THE SONG FIRST"); continue; }
                undo_mark(TDRUM, ui.step_sess ? ui.step_sess : (ui.step_sess = (undo_sess += 4u) | 3u));
                fm1_irq_off();
                if (k == 2) {
                    if (s > 0) dstep_set(st, drum_lane, LV_NORM, 0);
                    else {
                        dstep_clr(st, drum_lane);
                        drum_clear_empty(drum_cursor);
                    }
                } else if (dstep_has(st, drum_lane)) {
                    uint32_t r = (uint32_t)clamp((int32_t)lvl_rank(dstep_lvl(st, drum_lane)) + (s > 0 ? 1 : -1), 0, 3);
                    dstep_set(st, drum_lane, LV_UP[r], dstep_rat(st, drum_lane));
                }
                fm1_irq_on();
                sync_reload = 1;
            }
        } else {
            if (k == 0) TDRUM->p[P_E0] = (int16_t)clamp(TDRUM->p[P_E0] + s, 0, DRUM_KITS - 1);
            if (k == 1) song.g[G_DRLVL] = (int16_t)clamp(song.g[G_DRLVL] + s, 0, 127);
            if (k == 2) song.g[G_DRREV] = (int16_t)clamp(song.g[G_DRREV] + s, 0, 127);
            if (k == 3) TDRUM->p[P_PAN] = (int16_t)clamp(TDRUM->p[P_PAN] + s, -64, 63);
        }
    }
}

/* ---------------------------------------------------------------- REC --- */
/* a big seven-segment digit, TE style: w x h, segments t thick */
static void te_digit(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint32_t d, uint16_t c)
{
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   /* gfedcba */
    uint32_t m = SEG[d % 10u];
    int32_t hh = h / 2;
    if (m & 0x01) cv_rect(x + t, y, w - 2 * t, t, c);                       /* a */
    if (m & 0x02) cv_rect(x + w - t, y + t, t, hh - t, c);                  /* b */
    if (m & 0x04) cv_rect(x + w - t, y + hh, t, hh - t, c);                 /* c */
    if (m & 0x08) cv_rect(x + t, y + h - t, w - 2 * t, t, c);               /* d */
    if (m & 0x10) cv_rect(x, y + hh, t, hh - t, c);                         /* e */
    if (m & 0x20) cv_rect(x, y + t, t, hh - t, c);                          /* f */
    if (m & 0x40) cv_rect(x + t, y + hh - t / 2, w - 2 * t, t, c);          /* g */
}

static uint8_t rec_shown;
/* the four tracks, compact, from y0: the one that records is framed in red */
static void rec_rows(uint32_t rt, uint32_t take, uint32_t y0, uint32_t *cache)
{
    uint32_t sig = rt * 3u + rec_wait + take * 5u + drum_kit() * 977u + y0, i, j;
    for (i = 0; i < NTRK; i++) {
        char b[16];
        if (i == TRK_DRUM) str_cpy(b, DRUM_KIT_NAMES[drum_kit()], sizeof b);
        else trk_short_name(i, b);
        sig = studio_hash(sig, b) + (uint32_t)trk[i].p[P_SLEN] * 31u;
        for (j = 0; j < NSTEP; j++) sig = sig * 3u + (uint32_t)trk_step_on(&trk[i], j);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 76, C_BLACK);
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, 64), armed = i == rt;
        int32_t y = (int32_t)i * 19;
        char b[16];
        if (armed) cv_rect(0, y + 2, 2, 14, TE_RED);     /* the track that records: a red mark */
        cv_rect(6, y + 4, 10, 10, armed ? TE_COL[i] : TE_DIM[i]);
        if (i == TRK_DRUM) str_cpy(b, DRUM_KIT_NAMES[drum_kit()], sizeof b);
        else trk_short_name(i, b);
        te_upfit(b, b, sizeof b, 100);                  /* cut by width, before the steps at 126 */
        cv_text(22, y + 1, &FONT_S, b, armed ? C_WHITE : TE_G3);
        for (j = 0; j < 16u; j++) {
            uint32_t a = j * len / 16u, z = (j + 1u) * len / 16u, k, on = 0;
            if (z == a) z = a + 1u;
            for (k = a; k < z && k < NSTEP; k++) if (trk_step_on(t, k)) on = 1;
            cv_rect(126 + (int32_t)j * 7, y + 5, 5, 8, on ? (armed ? TE_COL[i] : TE_DIM[i]) : TE_G1);
        }
    }
    cv_blit(0, y0);
}

/* REC armed (stopped: waiting for the first note, or PLAY), its count-in, or a free take running
 * (seq.c). Armed, the dials say how it records: KNOB 1 MODE free / tempo (an empty project),
 * KNOB 2 LENGTH (1, 2 or 4 bars), KNOB 3 START note / count (ui_input.c rec_knobs). The four tracks
 * stay in view; the REC LED blinks while armed, is lit during the take. */
static void rec_screen_draw(void)
{
    static uint32_t head, body, rows, dials;
    static uint8_t layout;
    uint32_t sig, take = ft_on, empty = take || project_empty(), rt = take ? ft_trk % NTRK : song.sel;
    uint32_t blink = (fm1_ms / 250u) & 1u, secs = take ? ft_t * CTL / FS : 0u, bars = 0, bpm = 0;
    uint32_t lay = take ? 1u : 2u, free = empty && !rec_tempo && !take, count = ci_on;
    if (take)
        bars = ft_fit(ft_t, &bpm);
    if (!rec_shown || lay != layout) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
        ui_hint(take ? "REC: CLOSE  PLAY: DROP" : "REC: CANCEL");   /* (the keys, once) */
    }
    rec_shown = 1;
    layout = (uint8_t)lay;
    ui_head(take ? "FREE TAKE" : count ? "COUNT-IN" : "REC", TE_RED, 0, HEAD_BPM, &head);
    if (take) {                                         /* free take: the time, the loop it makes */
        sig = 1000003u + rt * 7919u + blink * 31u + secs * 131u + bars * 17u + bpm * 3u;
        if (ui.force || sig != body) {
            char b[24];
            uint32_t n = secs > 99u ? 99u : secs;
            body = sig;
            cv_begin(240, 106, C_BLACK);
            te_disc(18, 30, 9, blink ? TE_RED : TE_DIM[3]);
            if (n >= 10u)
                te_digit(36, 4, 32, 56, 7, n / 10u, C_WHITE);
            te_digit(76, 4, 32, 56, 7, n % 10u, C_WHITE);
            cv_text(112, 44, &FONT_S, "s", TE_G3);
            cv_rect(132, 6, 1, 54, TE_G1);
            if (bars) {
                fmt_int(b, (int32_t)bars);
                cv_text(146, 4, &FONT_L, b, TE_COL[rt & 3u]);
                cv_text(146 + text_w(&FONT_L, b) + 6, 18, &FONT_S, bars == 1u ? "BAR" : "BARS", TE_G3);
                fmt_int(b, (int32_t)bpm);
                cv_text(146, 42, &FONT_S, b, C_WHITE);
                cv_text(146 + text_w(&FONT_S, b) + 4, 42, &FONT_S, "BPM", TE_G3);
            } else {
                cv_text(146, 4, &FONT_L, "-", TE_G3);
            }
            te_text_c(120, 80, "PRESS REC ON THE 1", TE_RED);
            cv_blit(0, 42);
        }
        rec_rows(rt, take, 148u, &rows);
        return;
    }
    /* armed / counting in: what happens next */
    sig = 2000003u + empty * 7u + free * 11u + rec_count * 13u + count * 17u + ci_beat * 19u + blink * 31u;
    if (ui.force || sig != body) {
        /* what to do, and what follows (the keys: the header's hint) */
        static const char *const L1[3] = {"PLAY FREELY", "PLAY A NOTE", "PRESS PLAY"};
        static const char *const L2[3] = {"REC ON THE 1 CLOSES IT", "IT STARTS THE LOOP", "4 CLICKS, THEN REC"};
        uint32_t m = free ? 0u : rec_count ? 2u : 1u;
        body = sig;
        cv_begin(240, 64, C_BLACK);
        if (count) {                                    /* the count-in: 4, 3, 2, 1 */
            te_digit(105, 4, 30, 54, 6, 4u - (ci_beat > 3u ? 3u : ci_beat), C_WHITE);
            te_disc(70, 31, 12, TE_RED);
        } else {
            te_disc(28, 30, 18, blink ? TE_RED : TE_DIM[3]);
            te_disc(28, 30, 7, C_BLACK);
            cv_text(60, 12, &FONT_S, L1[m], C_WHITE);
            cv_text(60, 32, &FONT_S, L2[m], TE_G3);
        }
        cv_blit(0, 42);
    }
    rec_rows(rt, 0u, 106u, &rows);
    {   /* the dials: how it records */
        static char v[3][10];
        static const char *const LAB_E[4] = {"mode", "length", "start", ""};
        static const char *const LAB_F[4] = {"mode", "", "", ""};
        static const char *const LAB_N[4] = {"", "length", "start", ""};
        static const char *const LAB_0[4] = {"", "", "", ""};
        const char *val[4] = {v[0], v[1], v[2], ""};
        int32_t ratio[4] = {0, 0, 0, 0};
        uint32_t len = (uint32_t)clamp(TSEL->p[P_SLEN], 1, 64);
        str_cpy(v[0], rec_tempo ? "tempo" : "free", sizeof v[0]);
        if (len % 16u == 0u) {
            fmt_int(v[1], (int32_t)(len / 16u));
            str_cpy(v[1] + str_len(v[1]), len == 16u ? " bar" : " bars", 6);
        } else {
            fmt_int(v[1], (int32_t)len);
            str_cpy(v[1] + str_len(v[1]), " st", 4);
        }
        str_cpy(v[2], rec_count ? "count" : "note", sizeof v[2]);
        ratio[0] = ratio[1] = ratio[2] = NO_DIAL;      /* (choices, not amounts) */
        {
            const char *const *lab = count ? LAB_0 : !empty ? LAB_N : free ? LAB_F : LAB_E;
            uint32_t k;
            for (k = 0; k < 4u; k++)
                if (!lab[k][0])
                    val[k] = "";                        /* (a dial not shown: no value either) */
            te_dials(184, lab, val, ratio, rt * 7u + count * 3u, &dials);
        }
    }
}


#include "orbit_ui.c"

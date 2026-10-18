/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Small-canvas renderer (no full framebuffer). Draw text/lines into
 * an off-screen strip, then blit it in one DMA transfer. Pixels are stored
 * byte-swapped (the panel takes RGB565 big-endian). */
typedef struct {               /* proportional, see tools/gen_font.py */
    uint8_t h;
    uint8_t pad;               /* bitmap starts this many pixels left of the pen */
    uint8_t first, last;
    uint8_t sh;                /* 1: the bitmaps are another font's, drawn at 2x (L = S at 2x, no copy) */
    uint8_t bpp;               /* 1: packed binary alpha; 2 or 4: row-aligned alpha */
    const uint8_t *adv;        /* advance per glyph */
    const uint8_t *bw;         /* bitmap width per glyph (starts FONT_PAD left of the pen) */
    const uint16_t *off;       /* byte offset of each glyph: first inked row, inked rows, the rows */
    const uint8_t *data;
} felucca_font_t;
#include "felucca_font.h"

#define CV_MAX (240u * 124u)      /* the graph strip is 240 x 124 */
static uint16_t cv_px[CV_MAX] __attribute__((section(".pool")));
static uint32_t cv_w, cv_h;
static int32_t cv_jx, cv_jy;
static int32_t cv_oy;            /* y offset for graph drawing */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_BLACK 0x0000u
#define C_WHITE 0xFFFFu              /* accent only: what is being touched / where we are */
/* The screen is five steps of one colour, darkest to brightest, plus white.
 * Palettes are picked in the HOME-hold menu (COLOR). */
typedef struct {
    const char *name;
    uint16_t c[5];
} palette_t;
static const palette_t PALETTES[] = {
    {"GREEN", {RGB(0, 40, 12), RGB(0, 84, 30), RGB(16, 140, 54), RGB(56, 200, 92), RGB(120, 255, 146)}},
    {"AMBER", {RGB(60, 26, 0), RGB(110, 50, 0), RGB(170, 82, 0), RGB(225, 120, 8), RGB(255, 166, 40)}},
    {"CYAN", {RGB(0, 30, 50), RGB(0, 62, 96), RGB(16, 112, 160), RGB(56, 172, 222), RGB(140, 222, 255)}},
    {"RED", {RGB(52, 8, 8), RGB(100, 18, 14), RGB(170, 36, 26), RGB(226, 64, 48), RGB(255, 112, 92)}},
    {"MONO", {RGB(40, 40, 40), RGB(80, 80, 80), RGB(130, 130, 130), RGB(186, 186, 186), RGB(226, 226, 226)}},
    {"ORBIT", {RGB(30, 30, 44), RGB(70, 72, 100), RGB(119, 121, 150), RGB(174, 177, 220), RGB(223, 217, 255)}},   /* the OP-1's lavender grays */
    {"PASTEL", {RGB(38, 38, 46), RGB(90, 90, 108), RGB(150, 150, 170), RGB(202, 202, 220), RGB(236, 236, 248)}},
    {"NEON", {RGB(30, 32, 44), RGB(76, 86, 106), RGB(132, 148, 172), RGB(188, 210, 226), RGB(226, 246, 255)}},
};
#define NPALETTES (sizeof(PALETTES) / sizeof(PALETTES[0]))
static uint16_t pal[5], TE_COL[4], TE_MID[4], TE_DIM[4], te_alert, te_g[4];
/* ORBIT(기본): 오리지널 OP-1 화면의 encoder 색 blue / green / white / red (OP-1 화면에서 잰 값). 셋째 색은 흰색이다.
 * PASTEL과 NEON은 사용자가 고르는 팔레트로 자기 색을 유지한다 */
static const uint16_t TRACK_STYLES[3][4] = {
    {RGB(105,142,255), RGB(0,237,149), RGB(255,255,255), RGB(255,58,93)},
    {RGB(142,182,255), RGB(140,224,176), RGB(255,222,140), RGB(255,166,150)},
    {RGB(52,210,255), RGB(114,255,100), RGB(244,255,60), RGB(255,86,194)},
};
static uint16_t color_scale(uint16_t c, uint32_t n)
{
    return (uint16_t)(((((c >> 11) & 31u) * n / 255u) << 11) |
        ((((c >> 5) & 63u) * n / 255u) << 5) | ((c & 31u) * n / 255u));
}
#define C_LINE pal[0]                /* 1 rules, separators */
#define C_DIM pal[1]                 /* 2 inactive, empty steps, units */
#define C_GRAY pal[2]                /* 3 labels */
#define C_AMB pal[3]                 /* 4 secondary text */
#define C_HI pal[4]                  /* 5 values, curves */

static void palette_set(uint32_t i)
{
    uint32_t k;
    i %= NPALETTES;
    for (k = 0; k < 5u; k++)
        pal[k] = PALETTES[i].c[k];
    for (k = 0; k < 4u; k++) {
        TE_COL[k] = i >= 5u ? TRACK_STYLES[i - 5u][k] : pal[4u - (k & 1u)];
        TE_MID[k] = color_scale(TE_COL[k], 170u);
        TE_DIM[k] = color_scale(TE_COL[k], 86u);
    }
    te_alert = i == 4u ? C_WHITE : i == 5u ? TRACK_STYLES[0][3] : RGB(255,44,52);
    {   /* the live screens' neutrals (tiles, tracks, labels, secondary text): lavender with ORBIT, gray else */
        static const uint16_t G[2][4] = {{RGB(26, 26, 30), RGB(54, 54, 60), RGB(118, 118, 126), RGB(196, 196, 204)},
                                         {RGB(22, 22, 34), RGB(50, 51, 74), RGB(119, 121, 150), RGB(174, 177, 220)}};
        for (k = 0; k < 4u; k++)
            te_g[k] = G[i == 5u][k];
    }
}

static inline uint16_t swap16(uint32_t c) { return (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8)); }

static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    uint32_t i, n;
    if (w * h > CV_MAX)
        h = CV_MAX / w;
    lcd_sync();                     /* the last blit may still read cv_px */
    cv_w = w;
    cv_h = h;
    cv_jx = cv_jy = -99999;
    n = w * h;
    for (i = 0; i < n; i++)
        cv_px[i] = swap16(bg);
}

static __attribute__((noinline)) void cv_blit(uint32_t x, uint32_t y) { lcd_blit(x, y, cv_w, cv_h, cv_px); }

/* canvas rows r0 .. cv_h-1 only, to screen row y + r0 */
static void cv_blit_from(uint32_t x, uint32_t y, uint32_t r0)
{
    if (r0 < cv_h)
        lcd_blit(x, y + r0, cv_w, cv_h - r0, cv_px + r0 * cv_w);
}

static inline void cv_pset(int32_t x, int32_t y, uint16_t c)
{
    y += cv_oy;
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(c);
}

static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w, y1 = y + h + cv_oy, i;
    uint16_t sc = swap16(c);
    y += cv_oy;                             /* clipped once, then filled row by row */
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > (int32_t)cv_w)
        x1 = (int32_t)cv_w;
    if (y1 > (int32_t)cv_h)
        y1 = (int32_t)cv_h;
    for (; y < y1; y++)
        for (i = x; i < x1; i++)
            cv_px[(uint32_t)y * cv_w + (uint32_t)i] = sc;
}

/* ------------------------------------------------------------------------
 * Anti-aliased drawing. Coverage is computed per pixel from an exact
 * distance (pixel centre to the shape) and blended onto what is already in
 * the canvas, so no extra buffer is needed. Coordinates ending in _q4 are in
 * 1/16 pixel; pixel (x, y) has its centre at (16x + 8, 16y + 8).
 * ------------------------------------------------------------------------ */
#ifndef CV_AA
#define CV_AA 1
#endif
static int32_t cv_lw = 20;          /* stroke width for diagonal lines, 1/16 px (1.25 px) */
/* cv_jx, cv_jy: end of the last line (1/16 px), a polyline joint; reset by cv_begin */

static inline uint16_t unswap16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

/* blend colour c onto the canvas with coverage a (0..256) */
static inline void cv_blend(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint16_t *p;
    uint32_t d, r, g, b;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || (uint32_t)y >= cv_h || !a)
        return;
    p = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    if (a >= 256u) {
        *p = swap16(c);
        return;
    }
    d = unswap16(*p);
    if ((d >> 11) == (d & 31u) && ((d >> 6) & 31u) == (d & 31u) &&
        (c >> 11) == (c & 31u) && ((c >> 6) & 31u) == (c & 31u)) {
        /* gray onto gray (MONO): blend one level, so overlaps don't drift green */
        g = (((d >> 5) & 63u) * (256u - a) + ((c >> 5) & 63u) * a) >> 8;
        g = g * 255u / 63u;
        *p = swap16(RGB(g, g, g));
        return;
    }
    r = ((d >> 11) * (256u - a) + (c >> 11) * a) >> 8;
    g = (((d >> 5) & 63u) * (256u - a) + ((c >> 5) & 63u) * a) >> 8;
    b = ((d & 31u) * (256u - a) + (c & 31u) * a) >> 8;
    *p = swap16((r << 11) | (g << 5) | b);
}

static uint32_t gfx_isqrt(uint32_t v)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v)
        bit >>= 2;
    for (; bit; bit >>= 2) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else
            r >>= 1;
    }
    return r;
}

/* coverage of a pixel whose centre is dist (1/16 px) from the stroke centre,
 * for a stroke of half width hw (1/16 px): a 1 px box filter */
static inline uint32_t cov_of(int32_t dist, int32_t hw)
{
    int32_t a = hw + 8 - dist;          /* (hw + 0.5 - d) in 1/16 px */
    if (a <= 0)
        return 0;
    return a >= 16 ? 256u : (uint32_t)a * 16u;
}

/* a line of width w (1/16 px) between two points given in 1/16 px; round
 * caps, except at a polyline joint (the start is the previous line's end),
 * where the start cap is left out so the joint is not drawn twice */
static void cv_line_q4(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, uint16_t c)
{
    int32_t dx = x1 - x0, dy = y1 - y0, hw = w / 2, len, r, lo, hi, i, j, ext;
    int32_t steep = (dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx);
    uint32_t len2 = (uint32_t)(dx * dx + dy * dy);
    int startcap = !(x0 == cv_jx && y0 == cv_jy);
    cv_jx = x1;
    cv_jy = y1;
    {   /* wholly outside this canvas (another band, cv_oy): nothing to compute */
        int32_t m = hw + 24;
        if (((y0 < y1 ? y1 : y0) + m) / 16 + cv_oy < 0 || ((y0 < y1 ? y0 : y1) - m) / 16 + cv_oy >= (int32_t)cv_h ||
            ((x0 < x1 ? x1 : x0) + m) / 16 < 0 || ((x0 < x1 ? x0 : x1) - m) / 16 >= (int32_t)cv_w)
            return;
    }
    len = (int32_t)gfx_isqrt(len2);
    r = (hw + 8 + 15) >> 4;             /* pixels to test on each side of the line */
    ext = r + 1;
    /* walk the major axis one pixel at a time, test a small window across it */
    if (!steep) {
        lo = ((x0 < x1 ? x0 : x1) >> 4) - ext;
        hi = ((x0 < x1 ? x1 : x0) >> 4) + ext;
    } else {
        lo = ((y0 < y1 ? y0 : y1) >> 4) - ext;
        hi = ((y0 < y1 ? y1 : y0) >> 4) + ext;
    }
    for (i = lo; i <= hi; i++) {
        int32_t m = i * 16 + 8, mid;    /* centre of this column/row, 1/16 px */
        if (!steep)
            mid = dx ? y0 + (m - x0) * dy / dx : y0;
        else
            mid = dy ? x0 + (m - y0) * dx / dy : x0;
        {   /* clamp the window centre to the segment's span so caps are covered */
            int32_t a0 = steep ? x0 : y0, a1 = steep ? x1 : y1;
            int32_t mn = a0 < a1 ? a0 : a1, mx = a0 < a1 ? a1 : a0;
            if (mid < mn) mid = mn;
            if (mid > mx) mid = mx;
        }
        for (j = (mid >> 4) - r; j <= (mid >> 4) + r; j++) {
            int32_t px = steep ? j : i, py = steep ? i : j;
            int32_t qx = px * 16 + 8 - x0, qy = py * 16 + 8 - y0, dist;
            int32_t t = qx * dx + qy * dy;                  /* projection, 1/256 px^2 */
            if (len == 0 || t <= 0) {
                if (!startcap && len)
                    continue;
                dist = (int32_t)gfx_isqrt((uint32_t)(qx * qx + qy * qy));
            } else if (t >= (int32_t)len2) {
                int32_t ex = qx - dx, ey = qy - dy;
                dist = (int32_t)gfx_isqrt((uint32_t)(ex * ex + ey * ey));
            } else {
                int32_t cr = qx * dy - qy * dx;
                dist = (cr < 0 ? -cr : cr) / len;
            }
            cv_blend(px, py, c, cov_of(dist, hw));
        }
    }
}

/* a ring: centre and radius in 1/16 px, stroke width w (1/16 px) */
static void cv_ring_q4(int32_t cx, int32_t cy, int32_t rad, int32_t w, uint16_t c)
{
    int32_t hw = w / 2, rr = (rad + hw + 24) >> 4, x, y;
    int32_t ccx = cx >> 4, ccy = cy >> 4, ri = rad - hw - 8, ro = rad + hw + 8;
    uint32_t lo2 = ri > 0 ? (uint32_t)(ri * ri) : 0u, hi2 = (uint32_t)(ro * ro);
    for (y = ccy - rr; y <= ccy + rr; y++)
        for (x = ccx - rr; x <= ccx + rr; x++) {
            int32_t qx = x * 16 + 8 - cx, qy = y * 16 + 8 - cy, d;
            uint32_t d2 = (uint32_t)(qx * qx + qy * qy);
            if (d2 < lo2 || d2 > hi2)       /* outside the band: no square root */
                continue;
            d = (int32_t)gfx_isqrt(d2) - rad;
            cv_blend(x, y, c, cov_of(d < 0 ? -d : d, hw));
        }
}

/* a filled disc, centre and radius in 1/16 px */
static void cv_disc_q4(int32_t cx, int32_t cy, int32_t rad, uint16_t c)
{
    int32_t rr = (rad + 24) >> 4, x, y, ccx = cx >> 4, ccy = cy >> 4, ri = rad - 16;
    uint32_t in2 = ri > 0 ? (uint32_t)(ri * ri) : 0u, out2 = (uint32_t)(rad * rad);
    for (y = ccy - rr; y <= ccy + rr; y++)
        for (x = ccx - rr; x <= ccx + rr; x++) {
            int32_t qx = x * 16 + 8 - cx, qy = y * 16 + 8 - cy;
            uint32_t d2 = (uint32_t)(qx * qx + qy * qy);
            if (d2 > out2)
                continue;
            cv_blend(x, y, c, d2 <= in2 ? 256u : cov_of((int32_t)gfx_isqrt(d2), rad - 8 > 0 ? rad - 8 : 0));
        }
}

/* angle of (x, y) in 1/1024 turn, same convention as SINE (y down = clockwise) */
static uint32_t atan2_1024(int32_t y, int32_t x)
{
    int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, t, a;
    if (!ax && !ay)
        return 0;
    if (ax >= ay) {             /* atan(t) ~ t*128 + 44.5 t(1-t), t in Q12 */
        t = (ay << 12) / ax;
        a = (t * 128 + ((t * (4096 - t)) >> 12) * 89 / 2) >> 12;
    } else {
        t = (ax << 12) / ay;
        a = 256 - ((t * 128 + ((t * (4096 - t)) >> 12) * 89 / 2) >> 12);
    }
    if (x < 0)
        a = 512 - a;
    if (y < 0)
        a = 1024 - a;
    return (uint32_t)a & 1023u;
}

/* an arc band from angle a0 sweeping n units (1/1024 turn) clockwise, radius
 * rad (centre of the stroke) and width w, all in 1/16 px; round ends */
static void cv_arc_q4(int32_t cx, int32_t cy, int32_t rad, int32_t w, uint32_t a0, uint32_t n, uint16_t c)
{
    int32_t hw = w / 2, rr = (rad + hw + 24) >> 4, x, y, ccx = cx >> 4, ccy = cy >> 4;
    int32_t ri = rad - hw - 8, ro = rad + hw + 8;
    uint32_t k, lo2 = ri > 0 ? (uint32_t)(ri * ri) : 0u, hi2 = (uint32_t)(ro * ro);
    if (!n)
        return;
    for (y = ccy - rr; y <= ccy + rr; y++)
        for (x = ccx - rr; x <= ccx + rr; x++) {
            int32_t qx = x * 16 + 8 - cx, qy = y * 16 + 8 - cy, d;
            uint32_t d2 = (uint32_t)(qx * qx + qy * qy), rel;
            if (d2 < lo2 || d2 > hi2)
                continue;
            d = (int32_t)gfx_isqrt(d2) - rad;
            rel = (atan2_1024(qy, qx) - a0) & 1023u;
            if (rel <= n)
                cv_blend(x, y, c, cov_of(d < 0 ? -d : d, hw));
        }
    for (k = 0; k < 2u; k++) {          /* round ends: discs over the hard angular cut */
        uint32_t a = (a0 + (k ? n : 0u)) & 1023u;
        cv_disc_q4(cx + ((SINE[(a + 256u) & 1023u] * rad) >> 15), cy + ((SINE[a] * rad) >> 15), hw, c);
    }
}

/* A curve y = f(x), one sample per pixel column (y in 1/16 px). Each column
 * is covered from the midpoint with its left neighbour to the midpoint with
 * its right neighbour, widened by the stroke's vertical half thickness, and
 * box-filtered: a few pixels per column, no joints drawn twice. Feed points
 * left to right with cv_curve_pt, then cv_curve_end. */
static struct { int32_t x, y0, y1, n, hw; uint16_t c; } cvc;
static void cv_curve_begin(int32_t w, uint16_t c) { cvc.n = 0; cvc.hw = w / 2; cvc.c = c; }
static void cv_curve_col(int32_t x, int32_t yp, int32_t y, int32_t yn)
{
    int32_t a = (yp + y) / 2, b = (y + yn) / 2, s = (yn - yp) / 2, hwv, lo, hi, py;
    hwv = cvc.hw * (int32_t)gfx_isqrt((uint32_t)(256 + s * s)) / 16;   /* hw * sqrt(1 + slope^2) */
    lo = (a < b ? a : b) - hwv;
    hi = (a < b ? b : a) + hwv;
    if (y < lo + hwv) lo = y - hwv;         /* a peak: the sample itself sticks out */
    if (y > hi - hwv) hi = y + hwv;
    {   /* only the rows of this canvas (a band: cv_oy), whole pixels, so the coverage is the same */
        int32_t top = -cv_oy * 16, bot = ((int32_t)cv_h - cv_oy) * 16 - 1;
        if (lo < top) lo = top;
        if (hi > bot) hi = bot;
        if (lo > hi)
            return;
    }
    for (py = lo >> 4; py <= hi >> 4; py++) {
        int32_t t = py * 16, o = (hi < t + 16 ? hi : t + 16) - (lo > t ? lo : t);
        if (o > 0)
            cv_blend(x, py, cvc.c, o >= 16 ? 256u : (uint32_t)o * 16u);
    }
}
static void cv_curve_pt(int32_t x, int32_t y)
{
    if (cvc.n >= 2)
        cv_curve_col(cvc.x, cvc.y0, cvc.y1, y);
    else if (cvc.n == 1)
        cv_curve_col(cvc.x, cvc.y1, cvc.y1, y);
    cvc.y0 = cvc.n ? cvc.y1 : y;
    cvc.y1 = y;
    cvc.x = x;
    cvc.n++;
}
static void cv_curve_end(void)
{
    if (cvc.n)
        cv_curve_col(cvc.x, cvc.y0, cvc.y1, cvc.y1);
    cvc.n = 0;
}

static void cv_line_px(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c);
static void cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
#if CV_AA
    if (x0 != x1 && y0 != y1) {         /* diagonals: anti-aliased; rules stay pixel-exact */
        cv_line_q4(x0 * 16 + 8, y0 * 16 + 8, x1 * 16 + 8, y1 * 16 + 8, cv_lw, c);
        return;
    }
    cv_jx = x1 * 16 + 8;
    cv_jy = y1 * 16 + 8;
#endif
    cv_line_px(x0, y0, x1, y1, c);
}

static void cv_line_px(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 2000;
    while (guard--) {                       /* bounded: a line is never longer than 480 px */
        int32_t e2 = 2 * err;               /* both tests use the same error value */
        cv_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* glyph index of a character: lower case folds to upper case when the font
 * has none, anything missing (and C1 controls) draws as '?' */
static uint32_t glyph(const felucca_font_t *f, uint32_t ch)
{
    if (ch >= 'a' && ch <= 'z' && f->last < 'a')
        ch -= 32u;
    if (ch < f->first || ch > f->last || (ch >= 127u && ch < 160u))
        ch = '?';
    return ch - f->first;
}

/* alpha 0..15 of glyph g (its two-byte row header, then the rows) at x, y of the cell */
static inline uint32_t font_alpha(const uint8_t *g, uint32_t w, uint32_t x, uint32_t y, uint32_t bpp)
{
    const uint8_t *data = g + 2;
    y -= g[0];
    if (y >= g[1])                  /* a blank row above or below the ink */
        return 0;
    if (bpp == 1u) {
        uint32_t bit = y * w + x;
        return ((data[bit >> 3] >> (7u - (bit & 7u))) & 1u) * 15u;
    }
    if (bpp == 2u)
        return ((data[y * ((w + 3u) / 4u) + x / 4u] >> (6u - 2u * (x & 3u))) & 3u) * 5u;
    {
        uint32_t v = data[y * ((w + 1u) / 2u) + x / 2u];
        return x & 1u ? v & 15u : v >> 4;
    }
}
/* text, alpha-blended onto black with colour c; returns the end x */
static int32_t cv_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    uint16_t ramp[16];
    uint32_t r = c >> 11, g = (c >> 5) & 63u, b = c & 31u, a;
    for (a = 0; a < 16u; a++)
        ramp[a] = (uint16_t)(((r * a / 15u) << 11) | ((g * a / 15u) << 5) | (b * a / 15u));
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w;
        const uint8_t *gd;
        w = f->bw[gi];
        gd = f->data + f->off[gi];
        for (gy = (uint32_t)gd[0] << f->sh; gy < (uint32_t)(gd[0] + gd[1]) << f->sh; gy++)   /* inked rows only */
            for (gx = 0; gx < w; gx++) {
                uint32_t v = font_alpha(gd, w >> f->sh, gx >> f->sh, gy >> f->sh, f->bpp);
#if CV_AA
                if (v)          /* blend onto what is there (tiles, rows), not onto black */
                    cv_blend(x - f->pad + (int32_t)gx, y + (int32_t)gy, c, v * 256u / 15u);
#else
                if (v)
                    cv_pset(x - f->pad + (int32_t)gx, y + (int32_t)gy, ramp[v]);
#endif
            }
        x += f->adv[gi];
    }
    return x;
}

static int32_t text_w(const felucca_font_t *f, const char *s)
{
    int32_t w = 0;
    for (; *s; s++)
        w += f->adv[glyph(f, (uint8_t)*s)];
    return w;
}

/* one-shot: text in a box, cleared to black, blitted */
static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const felucca_font_t *f, const char *s,
                          uint16_t c, int align)
{
    int32_t tw = text_w(f, s), tx = 0;
    cv_begin(w, f->h, C_BLACK);
    if (align == 1)
        tx = ((int32_t)w - tw) / 2;
    else if (align == 2)
        tx = (int32_t)w - tw;
    cv_text(tx, 0, f, s, c);
    cv_blit(x, y);
    lcd_sync();                     /* one-shots (boot, crash, UBOOT, update) finish here */
}


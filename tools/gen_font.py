#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Render the bitmap fonts to a C header (assets/fonts/):
  S  labels / units / status   Latin-1 32..255, 16 px cell
  L  large values / titles     32..95 (digits, signs, capitals), 32 px cell
Inter ExtraLight (SIL OFL 1.1), anti-aliased; the instances come from
tools/make_inter.py (tabular figures frozen into the character map). BDF
pixel fonts (Terminus) also work through render(), and a pixel font at 2x can
reuse the 1x font's bitmaps (sh = 1).

Glyph format: per glyph an advance width, a bitmap width and an offset; the
bitmap starts FONT_PAD pixels left of the pen position (room for side
bearings). At the offset: the first inked row of the cell and the number of
inked rows (blank rows are not stored), then the rows. Binary alpha uses
packed bits, most significant first, with continuous rows. Anti-aliased alpha
uses 2 bits a pixel (4 pixels a byte, most significant first, rows byte
aligned), alpha 0..3 drawn as 0, 5, 10, 15: 4-bit alpha does not fit the app
slot. Identical glyphs share storage.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONTS = Path(__file__).resolve().parents[1] / "assets" / "fonts"
# (name, file, px, scale, pixel, first, last, cell): pixel fonts are rendered without
# anti-aliasing at their design size and enlarged by an integer factor; cell is the
# line height (None: the font's own). The sizes give the tabular digits the advance of
# the old Terminus 8x16 fonts (8 and 16 px), so number layouts keep their widths.
SIZES = [("S", "Inter-ExtraLight-S.ttf", 13, 1, False, 32, 255, 16),   # Latin-1 (Hügelton needs the umlaut)
         ("L", "Inter-ExtraLight-L.ttf", 25, 1, False, 32, 95, 32)]    # values / titles: digits, signs, capitals
PAD = 2
AA_BPP = 2


def upscale(px_, w, h, scale):
    """integer nearest-neighbour enlargement of a w x h glyph"""
    if scale == 1:
        return px_
    big = []
    for y in range(h):
        row = []
        for x in range(w):
            row += [px_[y * w + x]] * scale
        big += row * scale
    return big


def render_bdf(path, scale, first, last):
    """BDF bitmap font (Terminus 8x16): exact pixels, cell = the font bounding box."""
    lines = path.read_text(errors="replace").splitlines()
    fbb = next(l for l in lines if l.startswith("FONTBOUNDINGBOX")).split()
    cw, ch, fx, fy = int(fbb[1]), int(fbb[2]), int(fbb[3]), int(fbb[4])
    base = ch + fy                                     # rows above the baseline
    glyphs, i = {}, 0
    while i < len(lines):
        if lines[i].startswith("ENCODING"):
            code = int(lines[i].split()[1])
            j = i
            while not lines[j].startswith("BBX"):
                j += 1
            bw, bh, bx, by = map(int, lines[j].split()[1:5])
            dw = cw
            k = i
            while not lines[k].startswith("BITMAP"):
                if lines[k].startswith("DWIDTH"):
                    dw = int(lines[k].split()[1])
                k += 1
            rows = [int(r, 16) for r in lines[k + 1:k + 1 + bh]]
            nbits = ((bw + 7) // 8) * 8
            cell = [0] * (cw * ch)
            top = base - (by + bh)
            for r, bits in enumerate(rows):
                for c in range(bw):
                    if bits >> (nbits - 1 - c) & 1:
                        x, y = bx + c, top + r
                        if 0 <= x < cw and 0 <= y < ch:
                            cell[y * cw + x] = 15
            glyphs[code] = (dw, cell)
            i = k + bh
        i += 1
    if 0xDC not in glyphs and ord("U") in glyphs:      # Ü for "HÜGELTON": U with dots on its top row
        dw, cell = glyphs[ord("U")]
        cell = cell[:]
        lit = [x for y in range(ch) for x in range(cw) if cell[y * cw + x]]
        if lit:
            top = min(y for y in range(ch) for x in range(cw) if cell[y * cw + x])
            for x in range(cw):
                cell[top * cw + x] = 15 if x in (min(lit), max(lit)) else 0
            for x in range(cw):
                cell[(top + 1) * cw + x] = 0
            glyphs[0xDC] = (dw, cell)
    missing = [chr(c) for c in range(first, min(last, 126) + 1) if c not in glyphs]
    if missing:
        print(f"font {path.name}: no glyph for {''.join(missing)!r} (drawn as '?')")
    out = []
    for c in range(first, last + 1):
        dw, cell = glyphs.get(c, glyphs[ord("?")])
        bw = cw + 2 * PAD                              # keep the PAD convention of the TTF path
        px_ = []
        for y in range(ch):
            px_ += [0] * PAD + cell[y * cw:(y + 1) * cw] + [0] * PAD
        out.append((dw * scale, bw * scale, upscale(px_, bw, ch, scale)))
    return ch * scale, out


def render(ttf, px, scale, pixel, first, last, cell=None):
    if ttf.endswith(".bdf"):
        return render_bdf(FONTS / ttf, scale, first, last)
    font = ImageFont.truetype(str(FONTS / ttf), px)    # tabular figures are in the cmap (make_inter.py)
    asc, desc = font.getmetrics()
    imgs = []
    for c in range(first, last + 1):
        ch = "?" if 127 <= c < 160 else chr(c)         # C1 controls: never drawn (gfx.c glyph() uses '?')
        adv = int(round(font.getlength(ch)))
        bw = adv + 2 * PAD
        img = Image.new("L", (bw, asc + desc), 0)
        ImageDraw.Draw(img).text((PAD, 0), ch, font=font, fill=255)
        imgs.append((adv, bw, img))
    if cell:                                           # the cell ends at the lowest ink of the set
        bottom = max(img.getbbox()[3] for _, _, img in imgs if img.getbbox())
        top, h = bottom - cell, cell
        lost = [chr(first + i) for i, (_, _, img) in enumerate(imgs) if img.getbbox() and img.getbbox()[1] < top]
        if lost:
            print(f"font {ttf} {px}px: the tops of {''.join(lost)!r} are cut by the {cell} px cell")
    else:
        top = max(0, font.getbbox("A8|(")[1] - 1)
        h = asc + desc - top
    glyphs = []
    for adv, bw, img in imgs:
        img = img.crop((0, top, bw, top + h))
        px_ = [(15 if v >= 128 else 0) if pixel else min(15, (v + 8) // 17) for v in img.tobytes()]
        glyphs.append((adv * scale, bw * scale, upscale(px_, bw, h, scale)))
    return h * scale, glyphs


def main(out):
    lines = [f"/* generated by tools/gen_font.py from {', '.join(sorted({s[1] for s in SIZES}))} */",
             "#pragma once", "#include <stdint.h>", f"#define FONT_PAD {PAD}  /* x scale for L, see below */", ""]
    base = None                                        # (name, first, h, glyphs) of the 1x font
    for name, ttf, px, scale, pixel, first, last, cell in SIZES:
        h, glyphs = render(ttf, px, scale, pixel, first, last, cell)
        if scale > 1 and pixel and base is not None:
            # SLOOP 2.4: a pixel font at 2x is the 1x font's own bitmaps, enlarged while drawing (gfx.c
            # cv_text): no second copy in flash (24 KB). Same glyphs, same pixels: checked here.
            bname, bfirst, bh, bglyphs, bpp = base
            assert scale == 2 and first == bfirst and h == bh * 2, "L must be S at 2x, same first glyph"
            for (adv, bw, g), (badv, bbw, bg) in zip(glyphs, bglyphs):
                assert adv == badv * 2 and bw == bbw * 2 and g == upscale(bg, bbw, bh, 2)
            lines.append(f"static const uint8_t FONT_{name}_ADV[{len(glyphs)}] = {{" +
                         ", ".join(str(a) for a, _, _ in glyphs) + "};")
            lines.append(f"static const uint8_t FONT_{name}_BW[{len(glyphs)}] = {{" +
                         ", ".join(str(b) for _, b, _ in glyphs) + "};")
            lines.append(f"static const felucca_font_t FONT_{name} = {{ {h}, {PAD * scale}, {first}, {last}, 1, {bpp}, "
                         f"FONT_{name}_ADV, FONT_{name}_BW, FONT_{bname}_OFF, FONT_{bname}_DATA }};")
            lines.append("")
            print(f"font {name}: {ttf} {px}px h {h}, digit adv {glyphs[ord('0') - first][0]}, "
                  f"the {bname} bitmaps at {scale}x (0 B)")
            continue
        bpp = 1 if all(v in (0, 15) for _, _, g in glyphs for v in g) else AA_BPP
        data, offs = [], []
        seen: dict[bytes, int] = {}
        for adv, bw, g in glyphs:
            if bpp == 2:
                g = [(v * 3 + 7) // 15 for v in g]     # 0..15 to the nearest of 0, 5, 10, 15
            rows = [y for y in range(h) if any(g[y * bw:(y + 1) * bw])]
            top = rows[0] if rows else 0
            n = rows[-1] + 1 - top if rows else 0
            assert top < 256 and n < 256
            g = g[top * bw:(top + n) * bw]
            blob = bytearray([top, n])
            if bpp == 1:
                for i in range(0, len(g), 8):
                    blob.append(sum((v // 15) << (7 - bit) for bit, v in enumerate(g[i:i + 8])))
                assert [15 * ((blob[2 + i // 8] >> (7 - i % 8)) & 1) for i in range(len(g))] == g
            elif bpp == 2:
                for y in range(n):
                    row = g[y * bw:(y + 1) * bw] + [0, 0, 0]
                    for x in range(0, bw, 4):
                        blob.append((row[x] << 6) | (row[x + 1] << 4) | (row[x + 2] << 2) | row[x + 3])
            else:
                for y in range(n):
                    row = g[y * bw:(y + 1) * bw] + [0]
                    for x in range(0, bw, 2):
                        blob.append((row[x] << 4) | row[x + 1])
            packed = bytes(blob)
            if packed not in seen:
                seen[packed] = len(data)
                data.extend(packed)
            offs.append(seen[packed])
        assert len(data) < 65536
        lines.append(f"static const uint8_t FONT_{name}_DATA[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
        lines.append("};")
        lines.append(f"static const uint16_t FONT_{name}_OFF[{len(offs)}] = {{" + ", ".join(map(str, offs)) + "};")
        lines.append(f"static const uint8_t FONT_{name}_ADV[{len(glyphs)}] = {{" +
                     ", ".join(str(a) for a, _, _ in glyphs) + "};")
        lines.append(f"static const uint8_t FONT_{name}_BW[{len(glyphs)}] = {{" +
                     ", ".join(str(b) for _, b, _ in glyphs) + "};")
        lines.append(f"static const felucca_font_t FONT_{name} = {{ {h}, {PAD * scale}, {first}, {last}, 0, {bpp}, "
                     f"FONT_{name}_ADV, FONT_{name}_BW, FONT_{name}_OFF, FONT_{name}_DATA }};")
        lines.append("")
        if scale == 1:
            base = (name, first, h, glyphs, bpp)
        print(f"font {name}: {ttf} {px}px h {h}, digit adv {glyphs[ord('0') - first][0]}, {len(data)} B ({bpp} bpp)")
    Path(out).write_text("\n".join(lines))


if __name__ == "__main__":
    main(sys.argv[1])


#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the screen font assets (assets/fonts/) from the Inter variable font.

  tools/make_inter.py Inter[opsz,wght].ttf

Inter (SIL OFL 1.1, https://github.com/rsms/inter) is a variable font; the
build renders static instances, so it needs no FreeType variation or libraqm
support. Each instance is fixed at one weight and optical size, has tabular
digits (the OpenType `tnum` feature) frozen into its character map, maps the
soft hyphen to the hyphen and keeps only Latin-1. Needs fontTools; run once
when the font or the instances change, not on every build.
"""
import sys
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

FONTS = Path(__file__).resolve().parents[1] / "assets" / "fonts"
# (file, weight, optical size): S labels at 14, L large values at 32 (Inter Display)
INSTANCES = [("Inter-ExtraLight-S.ttf", 200, 14),
             ("Inter-ExtraLight-L.ttf", 200, 32)]
LATIN1 = list(range(32, 127)) + list(range(160, 256))


def freeze_tnum(font):
    """point the digits at their tabular glyphs (single substitutions of `tnum`); signs and
    punctuation stay proportional, or "FM-1" reads "FM - 1\""""
    gsub = font["GSUB"].table
    subst = {}
    for rec in gsub.FeatureList.FeatureRecord:
        if rec.FeatureTag != "tnum":
            continue
        for i in rec.Feature.LookupListIndex:
            for st in gsub.LookupList.Lookup[i].SubTable:
                subst.update(getattr(st, "mapping", {}))
    assert subst, "no tnum substitutions"
    for table in font["cmap"].tables:
        if table.isUnicode():
            for cp in range(ord("0"), ord("9") + 1):
                table.cmap[cp] = subst.get(table.cmap[cp], table.cmap[cp])
            table.cmap.setdefault(0xAD, table.cmap[ord("-")])


def main(var):
    for name, wght, opsz in INSTANCES:
        font = instancer.instantiateVariableFont(TTFont(var), {"wght": wght, "opsz": opsz})
        freeze_tnum(font)
        opts = subset.Options()
        opts.layout_features = []              # tnum is in the character map now
        opts.name_IDs = ["*"]                   # keep the copyright and license records
        sub = subset.Subsetter(opts)
        sub.populate(unicodes=LATIN1)
        sub.subset(font)
        font.save(FONTS / name)
        print(f"{name}: wght {wght}, opsz {opsz}, {(FONTS / name).stat().st_size} B")


if __name__ == "__main__":
    main(sys.argv[1])

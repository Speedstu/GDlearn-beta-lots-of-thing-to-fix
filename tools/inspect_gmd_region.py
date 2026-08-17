#!/usr/bin/env python3
"""Inspect raw Geometry Dash objects around an x coordinate.

Useful when the exact solver repeatedly dies at the same early percentage: it
shows *all* raw objects (including decoration currently ignored by gmd_to_gdl)
next to the converter classification so a wrong ID/hitbox/rotation is visible.
"""
from __future__ import annotations

import argparse
from collections import Counter

from gmd_to_gdl import classify, parse_props, try_decompress, _f, _i


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("gmd")
    ap.add_argument("--x", type=float, required=True)
    ap.add_argument("--radius", type=float, default=180.0)
    args = ap.parse_args()

    raw = open(args.gmd, "rb").read()
    decoded = try_decompress(raw)
    if decoded is None:
        raise SystemExit("cannot decode gmd")
    text = decoded.decode("utf-8", "ignore")

    rows = []
    ids = Counter()
    for seg in text.strip().split(";")[1:]:
        if not seg or "," not in seg:
            continue
        p = parse_props(seg)
        oid = _i(p, 1, 0)
        x = _f(p, 2, 0.0)
        if oid <= 0 or abs(x - args.x) > args.radius:
            continue
        y = _f(p, 3, 0.0)
        rot = _f(p, 6, 0.0)
        scale = _f(p, 32, 1.0)
        sx = _f(p, 128, 1.0)
        sy = _f(p, 129, 1.0)
        no_touch = _i(p, 121, 0)
        obj = classify(p)
        kind = "decor"
        dims = "-"
        if obj is not None:
            kind = f"{obj.kind}:{obj.sub}"
            dims = f"{2*obj.hw:.2f}x{2*obj.hh:.2f}"
        rows.append((x, y, oid, rot, scale, sx, sy, no_touch, kind, dims))
        ids[oid] += 1

    rows.sort(key=lambda r: (r[0], r[1], r[2]))
    print("x       y       id    rot    scale sx    sy    nt  class       AABB")
    for r in rows:
        print(f"{r[0]:7.2f} {r[1]:7.2f} {r[2]:5d} {r[3]:6.1f} "
              f"{r[4]:5.2f} {r[5]:5.2f} {r[6]:5.2f} {r[7]:2d}  "
              f"{r[8]:11s} {r[9]}")
    print("\nIDs in region:")
    for oid, n in ids.most_common():
        print(f"  {oid}: {n}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

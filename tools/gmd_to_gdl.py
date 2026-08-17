#!/usr/bin/env python3
"""Convert Geometry Dash level strings / .gmd files into gdlearn2 .gdl.

The training core deliberately consumes a tiny deterministic text format.  All
of the brittle Geometry Dash parsing stays here so the C++ simulator remains
fast and dependency-free.

This converter accepts either:
  * an already-decoded inner level string (`header;obj;obj;...`), or
  * URL-safe base64 containing gzip/zlib/raw-deflate data.

`convert()` is intentionally importable by tools/fetch_gd_level.py.
"""
from __future__ import annotations

import argparse
import base64
import gzip
import math
import os
import zlib
from dataclasses import dataclass
from typing import Dict, Iterable, Optional, Tuple

# Gameplay IDs.  The conservative rule is important: unknown decoration must
# NOT become collision, otherwise a visually dense demon becomes impossible in
# the simulator.  These sets cover the official/classic object families and the
# gameplay interactives used by the bundled levels.
HAZARD_IDS = {
    8, 9, 39, 61, 103, 135, 143, 205,
    363, 364, 365, 392, 393, 394, 446, 447,
    667, 720, 721, 722, 768, 769, 989, 991,
}

# Only IDs known to participate in collision are solids. Geometry Dash
# has thousands of decoration objects; treating visual tiles as blocks
# creates fake walls and makes real levels unsolvable in the simulator.
# This list is deliberately conservative and mirrors the project's
# older parser for classic/official levels. Unknown IDs stay decoration
# until live hitbox calibration proves otherwise.
SOLID_IDS = set(range(1, 8)) | {
    40, 62, 63, 467, 468, 469,
    193, 194, 195, 196, 198, 199,
    204, 205, 206, 207, 208, 209,
    *range(247, 260),
    71, 72, 73, 74, 75, 76, 77, 78,
    *range(118, 130),
    *range(185, 193),
    *range(661, 696),
}

# gdl PortalKind enum values: cube, ship, ball, ufo, wave, robot, spider,
# swing, gravity-normal, gravity-flip, size-normal, size-mini.
PORTALS = {
    12: 0,              # cube
    13: 1, 47: 1, 111: 1,  # legacy parser aliases retained for old exports
    43: 2, 46: 2,
    747: 3,
    660: 4, 1049: 4,
    745: 5,
    1331: 6,
    1933: 7,
    10: 8,
    11: 9,
    99: 10,
    101: 11,
}
SPEEDS = {200: 0, 201: 1, 202: 2, 203: 3, 1334: 4}
# gdl PadKind: yellow, pink, red, blue.
PADS = {35: 0, 140: 2, 67: 3, 1332: 1, 1524: 2, 1697: 3}
# gdl OrbKind: yellow, pink, red, blue, green, black, dash.
ORBS = {36: 0, 141: 1, 1330: 2, 84: 3, 1022: 4, 1333: 5,
        1594: 6, 1704: 6, 1751: 6}


@dataclass
class GdlObject:
    kind: str
    sub: int
    x: float
    y: float
    hw: float
    hh: float
    oid: int


def _f(props: Dict[int, str], key: int, default: float) -> float:
    try:
        return float(props.get(key, default))
    except (TypeError, ValueError):
        return default


def _i(props: Dict[int, str], key: int, default: int) -> int:
    try:
        return int(float(props.get(key, default)))
    except (TypeError, ValueError):
        return default


def parse_props(segment: str) -> Dict[int, str]:
    parts = segment.split(',')
    out: Dict[int, str] = {}
    for i in range(0, len(parts) - 1, 2):
        try:
            out[int(parts[i])] = parts[i + 1]
        except ValueError:
            continue
    return out


def try_decompress(value) -> Optional[bytes]:
    """Return decoded inner-level bytes, or None when decoding is impossible."""
    if value is None:
        return None
    raw = value.encode() if isinstance(value, str) else bytes(value)
    stripped = raw.strip()
    if b';' in stripped and b',' in stripped:
        return stripped

    # GD uses URL-safe base64 and often omits padding.
    try:
        padded = stripped + b'=' * ((4 - len(stripped) % 4) % 4)
        blob = base64.urlsafe_b64decode(padded)
    except Exception:
        blob = stripped

    for decoder in (
        lambda b: gzip.decompress(b),
        lambda b: zlib.decompress(b),
        lambda b: zlib.decompress(b, -zlib.MAX_WBITS),
        lambda b: zlib.decompress(b, zlib.MAX_WBITS | 16),
    ):
        try:
            out = decoder(blob)
            if b';' in out:
                return out
        except Exception:
            pass
    if b';' in blob and b',' in blob:
        return blob
    return None


def _rotated_half_extents(hw: float, hh: float, degrees: float) -> Tuple[float, float]:
    r = math.radians(degrees % 360.0)
    c, s = abs(math.cos(r)), abs(math.sin(r))
    return hw * c + hh * s, hw * s + hh * c


def classify(props: Dict[int, str]) -> Optional[GdlObject]:
    oid = _i(props, 1, 0)
    if not oid:
        return None
    # Property 121 = no-touch on modern objects.  Treat it as decoration.
    if _i(props, 121, 0):
        return None

    x, y = _f(props, 2, 0.0), _f(props, 3, 0.0)
    scale = max(0.01, abs(_f(props, 32, 1.0)))
    sx = max(0.01, abs(_f(props, 128, scale)))
    sy = max(0.01, abs(_f(props, 129, scale)))
    rot = _f(props, 6, 0.0)

    if oid in PORTALS:
        return GdlObject('portal', PORTALS[oid], x, y, 12.0 * sx, 45.0 * sy, oid)
    if oid in SPEEDS:
        return GdlObject('speed', SPEEDS[oid], x, y, 10.0 * sx, 20.0 * sy, oid)
    if oid in PADS:
        return GdlObject('pad', PADS[oid], x, y, 15.0 * sx, 5.0 * sy, oid)
    if oid in ORBS:
        return GdlObject('orb', ORBS[oid], x, y, 18.0 * sx, 18.0 * sy, oid)
    if oid in HAZARD_IDS:
        hw, hh = _rotated_half_extents(4.0 * sx, 8.0 * sy, rot)
        return GdlObject('hazard', 0, x, y, hw, hh, oid)
    if oid in SOLID_IDS:
        hw, hh = _rotated_half_extents(15.0 * sx, 15.0 * sy, rot)
        return GdlObject('solid', 0, x, y, hw, hh, oid)
    return None


def convert(level_string: str, name: str = 'level', hitbox_dump: Optional[str] = None):
    decoded = try_decompress(level_string)
    if decoded is not None:
        level_string = decoded.decode('utf-8', 'ignore')

    segments = level_string.strip().split(';')
    objects = []
    # Segment 0 is level settings/header.  Object strings begin after it.
    for seg in segments[1:]:
        if not seg or ',' not in seg:
            continue
        obj = classify(parse_props(seg))
        if obj is not None:
            objects.append(obj)

    max_x = max((o.x + o.hw for o in objects), default=30.0)
    length = max_x + 150.0
    lines = [
        '# gdlearn level v1',
        f'name {name}',
        'floor 0',
        'roof 0',
        f'length {length:.6g}',
    ]
    for o in objects:
        lines.append(
            f'o {o.kind} {o.sub} {o.x:.6g} {o.y:.6g} '
            f'{o.hw:.6g} {o.hh:.6g} {o.oid}'
        )
    text = '\n'.join(lines) + '\n'

    if hitbox_dump:
        os.makedirs(os.path.dirname(os.path.abspath(hitbox_dump)), exist_ok=True)
        with open(hitbox_dump, 'w', encoding='utf-8') as fh:
            fh.write('kind,sub,x,y,hw,hh,id\n')
            for o in objects:
                fh.write(f'{o.kind},{o.sub},{o.x},{o.y},{o.hw},{o.hh},{o.oid}\n')
    return text, len(objects)


def main() -> int:
    ap = argparse.ArgumentParser(description='Convert Geometry Dash .gmd/level string to .gdl')
    ap.add_argument('input')
    ap.add_argument('-o', '--out')
    ap.add_argument('--name')
    ap.add_argument('--hitbox-dump')
    args = ap.parse_args()

    with open(args.input, 'rb') as fh:
        raw = fh.read()
    decoded = try_decompress(raw)
    if decoded is None:
        raise SystemExit(f'could not decode {args.input}')
    name = args.name or os.path.splitext(os.path.basename(args.input))[0]
    text, count = convert(decoded.decode('utf-8', 'ignore'), name, args.hitbox_dump)
    out = args.out or os.path.splitext(args.input)[0] + '.gdl'
    with open(out, 'w', encoding='utf-8') as fh:
        fh.write(text)
    print(f'{args.input} -> {out}: {count} gameplay objects')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

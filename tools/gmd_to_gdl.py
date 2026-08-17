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
    8, 9, 39, 61, 103, 135, 143, 205, 363, 364, 365, 392, 393, 394,
    446, 447, 667, 720, 721, 722, 768, 769, 989, 991, 1705, 1706, 1707,
}

# Broad solid families used through 2.1/early 2.2.  Slopes are deliberately
# approximated by their rotated/scaled AABB until live hitbox calibration is
# available; the fidelity tooling can later replace those boxes with measured
# engine rectangles.
SOLID_IDS = set(range(1, 8)) | {
    40, 62, 63, 64, 65, 66, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78,
    80, 81, 82, 83, 90, 91, 92, 93, 94, 95, 96, 116, 117, 118, 119,
    120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 170, 171, 172,
    173, 175, 176, 1820, 1821, 1823, 1824, 1825, 1826, 1827, 1828,
    185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196,
    197, 198, 199, 204, 206, 207, 208, 209, 210, 211, 212, 213,
    247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 258, 259,
    260, 261, 263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273,
    274, 275, 294, 295, 296, 297, 305, 307, 324, 325, 326, 327, 358,
    467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479,
    480, 481, 482, 485, 486, 487, 488, 489, 490, 491, 502,
    641, 642, 643, 644, 645, 646, 647, 648, 649, 650,
    661, 662, 663, 664, 681, 682, 683, 684, 685, 686, 687, 688,
    689, 690, 691, 692, 693, 694, 695, 696, 697, 698, 699, 700,
    703, 704, 705, 706, 707, 708, 713, 714, 715, 716, 730, 731, 732,
    733, 752, 753, 754, 755, 756, 757, 758, 759, 762, 763, 764, 765,
    766, 769, 770, 771, 772, 773, 774, 775, 807, 808, 809, 810, 811,
    812, 813, 814, 815, 816, 817, 818, 819, 820, 821, 822, 823, 824,
    825, 826, 827, 828, 829, 830, 831, 832, 833, 841, 842, 843, 844,
    845, 846, 847, 848, 850, 853, 854, 855, 856, 857, 859, 861, 862,
    863, 867, 868, 869, 870, 871, 872, 873, 874, 877, 878, 880, 881,
    882, 883, 884, 885, 888, 889, 890, 891, 893, 894, 895, 896,
    903, 904, 905, 911, 927, 928, 929, 930, 931, 932, 933, 934, 935,
    952, 953, 954, 955, 956, 957, 958, 959, 960, 961, 964, 965, 966,
    967, 968, 969, 970, 971, 972, 973, 974, 975, 976, 977, 1014,
    1015, 1016, 1017, 1018, 1033, 1034, 1035, 1036, 1037, 1038,
    1039, 1040, 1041, 1042, 1043, 1044, 1045, 1046, 1047, 1048,
    1062, 1075, 1076, 1077, 1083, 1084, 1085, 1086, 1087, 1088,
    1089, 1090, 1091, 1092, 1093, 1094, 1099, 1100, 1101, 1102,
    1103, 1104, 1105, 1106, 1107, 1108, 1109, 1112, 1113, 1114,
    1115, 1116, 1117, 1118, 1140, 1142, 1143, 1144, 1145, 1146,
    1147, 1148, 1149, 1150, 1151, 1152, 1153, 1154, 1155, 1156,
    1157, 1158, 1159, 1160, 1161, 1162, 1163, 1164, 1165, 1166,
    1167, 1168, 1169, 1170, 1171, 1172, 1173, 1174, 1175, 1176,
    1177, 1178, 1179, 1180, 1181, 1182, 1183, 1184, 1185, 1186,
    1191, 1192, 1193, 1194, 1195, 1196, 1197, 1202, 1203, 1204,
    1205, 1206, 1207, 1208, 1209, 1210, 1220, 1221, 1222, 1223,
    1224, 1225, 1226, 1227, 1229, 1230, 1231, 1232, 1233, 1234,
    1235, 1236, 1237, 1238, 1239, 1240, 1247, 1248, 1249, 1250,
    1251, 1252, 1253, 1254, 1255, 1256, 1257, 1258, 1259, 1260,
    1261, 1262, 1263, 1264, 1265, 1266, 1267, 1277, 1278, 1279,
    1280, 1281, 1282, 1283, 1284, 1285, 1286, 1287, 1288, 1289,
    1290, 1294, 1295, 1298, 1299, 1300, 1301, 1302, 1303, 1304,
    1305, 1306, 1307, 1308, 1309, 1322, 1338, 1339,
    1348, 1349, 1350, 1351, 1352, 1353, 1354, 1355, 1356, 1357,
    1358, 1359, 1360, 1361, 1362, 1363, 1364, 1365, 1366, 1387,
    1388, 1389, 1390, 1391, 1392, 1393, 1394, 1395, 1431, 1432,
    1433, 1434, 1435, 1436, 1437, 1438, 1439, 1440, 1441, 1442,
    1443, 1444, 1445, 1446, 1447, 1448, 1449, 1450, 1451, 1452,
    1461, 1462, 1463, 1464, 1471, 1472, 1473, 1496, 1507, 1510,
    1511, 1512, 1513, 1514, 1515, 1617, 1621, 1622, 1623, 1624,
    1625, 1626, 1627, 1628, 1629, 1630, 1631, 1632, 1633, 1634,
    1635, 1636, 1637, 1638, 1639, 1640, 1641, 1642, 1643, 1644,
    1645, 1646, 1647, 1648, 1649, 1650, 1651, 1652, 1685, 1686,
    1687, 1688, 1689, 1690, 1691, 1692, 1693, 1694, 1695, 1696,
    1743, 1744, 1745, 1746, 1747, 1748, 1749, 1750, 1769, 1770,
    1771, 1772, 1773, 1774, 1775, 1776, 1777, 1778, 1779, 1780,
    1781, 1782, 1783, 1784, 1785, 1786, 1787, 1788, 1789, 1790,
    1791, 1792, 1793, 1794, 1795, 1796, 1797, 1798, 1799, 1800,
    1801, 1802, 1803, 1804, 1805, 1806, 1807, 1808, 1809, 1810,
    1861, 1862, 1863, 1864, 1865, 1866, 1867, 1868, 1869, 1870,
    1871, 1872, 1874, 1875, 1876, 1877, 1878, 1879, 1880, 1881,
    1882, 1883, 1884, 1885, 1893, 1894, 1895, 1896, 1897, 1898,
    1899, 1900, 1901, 1902,
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

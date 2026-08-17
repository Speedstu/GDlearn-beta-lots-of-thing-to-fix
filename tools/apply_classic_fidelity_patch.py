#!/usr/bin/env python3
"""Apply measured classic Geometry Dash physics/collision calibration.

This helper is intentionally deterministic so CI can A/B the patch before it is
committed. It does not train anything; it only replaces old approximations with
measured classic gameplay IDs, hitboxes and cube motion constants.
"""
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f"expected {label} block not found")
    return text.replace(old, new, 1)


# ---------------------------------------------------------------- converter
p = Path("tools/gmd_to_gdl.py")
s = p.read_text()

start = s.index("PORTALS = {")
end = s.index("\n\n@dataclass", start)
new_maps = r'''PORTALS = {
    # Classic vehicle portals.
    12: 0,   # cube
    13: 1,   # ship
    47: 2,   # ball
    111: 3,  # UFO
    660: 4,  # wave
    # Later modes.
    745: 5,
    1331: 6,
    1933: 7,
    # Gravity / size.
    10: 8,
    11: 9,
    99: 10,
    101: 11,
}
SPEEDS = {200: 0, 201: 1, 202: 2, 203: 3, 1334: 4}
# gdl PadKind: yellow, pink, red, blue.
PADS = {35: 0, 140: 1, 1332: 2, 67: 3, 1524: 2, 1697: 3}
# gdl OrbKind: yellow, pink, red, blue, green, black, dash.
ORBS = {36: 0, 141: 1, 1333: 2, 84: 3, 1022: 4, 1330: 5,
        1594: 6, 1704: 6, 1751: 6}

# Full object hitbox sizes in world units for classic gameplay objects.
# Unknown visual IDs are never promoted to collision by these tables.
CLASSIC_SOLID_SIZE = {}
def _sizes(ids, w, h):
    for _id in ids:
        CLASSIC_SOLID_SIZE[_id] = (w, h)

_sizes([*range(1,5),6,7,63,*range(69,73),*range(74,79),*range(81,84),
        *range(90,97),*range(116,120),121,122,146,*range(160,164),
        *range(165,170),173,175,*range(207,211),212,213,
        *range(247,251),*range(252,259),260,261,*range(263,266),
        *range(267,273),274,275,467,469,470,471], 30.0, 30.0)
_sizes([64,195,206,220,661], 15.0, 15.0)
_sizes([40,147,215,369,370], 30.0, 14.0)
_sizes([170,171,172,174,192], 30.0, 21.0)
_sizes([468,475], 30.0, 1.5)
_sizes([62,65,66,68], 30.0, 16.0)
_sizes([196,219], 15.0, 8.0)
_sizes([204], 8.0, 15.0)
_sizes([662,663,664], 30.0, 15.0)
_sizes([328], 22.0, 22.0)
_sizes([197], 22.0, 21.0)
_sizes([194], 21.0, 21.0)
_sizes([176], 14.0, 21.0)
_sizes([34], 37.0, 23.0)

CLASSIC_HAZARD_SIZE = {
    720:(2.40039063,3.20001221), 991:(2.40039063,3.20001221),
    61:(9,7.2), 446:(9,7.2), 365:(9,6), 667:(9,6),
    392:(2.6,4.8), 8:(6,12), 103:(4,7.6), 39:(6,5.6),
    205:(6,5.6), 768:(4.5,5.2), 447:(5.2,7.2),
    135:(14.1,20), 422:(6,4.4), 244:(6,6.8), 243:(6,7.2),
    421:(9,5.2), 9:(9,10.8), 989:(9,12), 178:(6,6.4),
    179:(4,8), 919:(25,6),
}
PORTAL_SIZE = {
    12:(34,86), 13:(34,86), 47:(34,86), 111:(34,86), 660:(34,86),
    10:(25,75), 11:(25,75), 99:(31,90), 101:(31,90),
}
SPEED_SIZE = {200:(35,44), 201:(33,56), 202:(51,56),
              203:(65,56), 1334:(69,56)}
PAD_SIZE = {35:(25,4), 140:(25,5), 67:(25,6), 1332:(25,6)}'''
s = s[:start] + new_maps + s[end:]

start = s.index("def classify(props: Dict[int, str])")
end = s.index("\n\ndef convert(", start)
new_classify = r'''def classify(props: Dict[int, str]) -> Optional[GdlObject]:
    oid = _i(props, 1, 0)
    if not oid:
        return None
    if _i(props, 121, 0):
        return None

    x, y = _f(props, 2, 0.0), _f(props, 3, 0.0)
    scale = max(0.01, abs(_f(props, 32, 1.0)))
    sx = scale * max(0.01, abs(_f(props, 128, 1.0)))
    sy = scale * max(0.01, abs(_f(props, 129, 1.0)))
    rot = _f(props, 6, 0.0)

    if oid in PORTALS:
        w, h = PORTAL_SIZE.get(oid, (24.0, 90.0))
        hw, hh = _rotated_half_extents(0.5*w*sx, 0.5*h*sy, rot)
        return GdlObject('portal', PORTALS[oid], x, y, hw, hh, oid)
    if oid in SPEEDS:
        w, h = SPEED_SIZE[oid]
        hw, hh = _rotated_half_extents(0.5*w*sx, 0.5*h*sy, rot)
        return GdlObject('speed', SPEEDS[oid], x, y, hw, hh, oid)
    if oid in PADS:
        w, h = PAD_SIZE.get(oid, (25.0, 6.0))
        hw, hh = _rotated_half_extents(0.5*w*sx, 0.5*h*sy, rot)
        return GdlObject('pad', PADS[oid], x, y, hw, hh, oid)
    if oid in ORBS:
        hw, hh = _rotated_half_extents(18.0*sx, 18.0*sy, rot)
        return GdlObject('orb', ORBS[oid], x, y, hw, hh, oid)
    if oid in CLASSIC_HAZARD_SIZE:
        w, h = CLASSIC_HAZARD_SIZE[oid]
        hw, hh = _rotated_half_extents(0.5*w*sx, 0.5*h*sy, rot)
        return GdlObject('hazard', 0, x, y, hw, hh, oid)
    if oid in CLASSIC_SOLID_SIZE:
        w, h = CLASSIC_SOLID_SIZE[oid]
        hw, hh = _rotated_half_extents(0.5*w*sx, 0.5*h*sy, rot)
        return GdlObject('solid', 0, x, y, hw, hh, oid)

    # Modern/fallback objects not covered by the measured classic table.
    if oid in HAZARD_IDS:
        hw, hh = _rotated_half_extents(4.0*sx, 8.0*sy, rot)
        return GdlObject('hazard', 0, x, y, hw, hh, oid)
    if oid in SOLID_IDS:
        hw, hh = _rotated_half_extents(15.0*sx, 15.0*sy, rot)
        return GdlObject('solid', 0, x, y, hw, hh, oid)
    return None'''
s = s[:start] + new_classify + s[end:]
p.write_text(s)


# ---------------------------------------------------------------- physics
p = Path("src/core/physics.hpp")
s = p.read_text()
start = s.index("// ------------------------------------------------------------------ cube ----")
end = s.index("// ------------------------------------------------------------------ ship ----", start)
new_cube = r'''// ------------------------------------------------------------------ cube ----
// Measured/reverse-engineered game values converted directly from world
// units/second and world units/second^2 to native 240-TPS state units.
constexpr std::array<float, 5> CUBE_GRAVITY = {
    2747.52f / (TPS * TPS), 2794.1082f / (TPS * TPS),
    2786.4f / (TPS * TPS), 2799.36f / (TPS * TPS),
    2799.36f / (TPS * TPS),
};
constexpr std::array<float, 5> CUBE_JUMP = {
    573.481728f / TPS, 603.7217172f / TPS, 616.681728f / TPS,
    606.421728f / TPS, 606.421728f / TPS,
};
constexpr float CUBE_TERMINAL = 810.0f / TPS;
constexpr float CUBE_ROT_PER_FRAME = 1.5f;

'''
s = s[:start] + new_cube + s[end:]
s = replace_once(
    s,
    "constexpr float HITBOX_LETHAL_SCALE = 0.30f;\n",
    "constexpr float INNER_HITBOX_HALF = 4.5f;  // 9x9 frontal block hitbox\n",
    "inner hitbox constant",
)
p.write_text(s)


# ---------------------------------------------------------------- simulator
p = Path("src/core/sim.cpp")
s = p.read_text()
s = replace_once(
    s,
    '''      if (hold && st_.onGround) {
        st_.vy = phys::CUBE_JUMP * g;
        st_.onGround = false;
      }
      st_.vy -= phys::CUBE_GRAVITY * g;
      clampFall(phys::CUBE_TERMINAL);
''',
    '''      if (hold && st_.onGround) {
        const float miniJump = st_.mini ? 0.8f : 1.0f;
        st_.vy = phys::CUBE_JUMP[st_.tier] * miniJump * g;
        st_.onGround = false;
      }
      st_.vy -= phys::CUBE_GRAVITY[st_.tier] * g;
      clampFall(phys::CUBE_TERMINAL);
''',
    "cube motion",
)
s = replace_once(
    s,
    '''  const float hw = st_.halfW(), hh = st_.halfH();
  const float lhw = hw * (1.0f - phys::HITBOX_LETHAL_SCALE);
  const float lhh = hh * (1.0f - phys::HITBOX_LETHAL_SCALE);
''',
    '''  const float hw = st_.halfW(), hh = st_.halfH();
  // Full player box touches hazards; frontal block collision uses GD's much
  // smaller 9x9 inner box. Mini mode keeps the same inner-box dimensions.
  const float lhw = phys::INNER_HITBOX_HALF;
  const float lhh = phys::INNER_HITBOX_HALF;
''',
    "player collision hitboxes",
)
needle = '''  // Pass 2: hazards (inner hitbox), then solids, then boosts.
  for (int i = 0; i < n; ++i) {
    const Object& o = objs[idx[i]];
    if (o.kind != Kind::Hazard) continue;
    if (aabb(st_.x, st_.y, lhw, lhh, o.x, o.y, o.hw, o.hh)) {
'''
repl = '''  // Pass 2: hazards (outer hitbox), then solids, then boosts.
  for (int i = 0; i < n; ++i) {
    const Object& o = objs[idx[i]];
    if (o.kind != Kind::Hazard) continue;
    if (aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)) {
'''
s = replace_once(s, needle, repl, "hazard collision")
p.write_text(s)

print("classic fidelity patch applied")

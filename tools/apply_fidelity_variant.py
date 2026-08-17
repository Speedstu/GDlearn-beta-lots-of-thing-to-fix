#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from pathlib import Path


def replace_once(s: str, old: str, new: str, label: str) -> str:
    if old not in s:
        raise SystemExit(f"expected block not found: {label}")
    return s.replace(old, new, 1)


def patch_mappings(root: Path) -> None:
    p = root / "tools/gmd_to_gdl.py"
    s = p.read_text()
    start = s.index("PORTALS = {")
    end = s.index("\n\n\n@dataclass", start)
    block = '''PORTALS = {
    12: 0,   # cube
    13: 1,   # ship
    47: 2,   # ball
    111: 3,  # UFO
    660: 4,  # wave
    745: 5,  # robot
    1331: 6, # spider
    1933: 7, # swing
    10: 8,   # gravity normal
    11: 9,   # gravity flipped
    99: 10,  # normal size
    101: 11, # mini
}
SPEEDS = {200: 0, 201: 1, 202: 2, 203: 3, 1334: 4}
# gdl PadKind: yellow, pink, red, blue.
PADS = {35: 0, 140: 1, 1332: 2, 67: 3, 1524: 2, 1697: 3}
# gdl OrbKind: yellow, pink, red, blue, green, black, dash.
ORBS = {36: 0, 141: 1, 1333: 2, 84: 3, 1022: 4, 1330: 5,
        1594: 6, 1704: 6, 1751: 6}'''
    p.write_text(s[:start] + block + s[end:])


def patch_cube(root: Path) -> None:
    p = root / "src/core/physics.hpp"
    s = p.read_text()
    start = s.index("// ------------------------------------------------------------------ cube ----")
    end = s.index("// ------------------------------------------------------------------ ship ----", start)
    block = '''// ------------------------------------------------------------------ cube ----
// Reverse-engineered classic GD values, converted from world units/s and
// world units/s^2 into native 240-TPS state units.
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
    p.write_text(s[:start] + block + s[end:])

    p = root / "src/core/sim.cpp"
    s = p.read_text()
    old = '''      if (hold && st_.onGround) {
        st_.vy = phys::CUBE_JUMP * g;
        st_.onGround = false;
      }
      st_.vy -= phys::CUBE_GRAVITY * g;
      clampFall(phys::CUBE_TERMINAL);
'''
    new = '''      if (hold && st_.onGround) {
        const float miniJump = st_.mini ? 0.8f : 1.0f;
        st_.vy = phys::CUBE_JUMP[st_.tier] * miniJump * g;
        st_.onGround = false;
      }
      st_.vy -= phys::CUBE_GRAVITY[st_.tier] * g;
      clampFall(phys::CUBE_TERMINAL);
'''
    p.write_text(replace_once(s, old, new, "cube integration constants"))


def patch_inner_block(root: Path) -> None:
    p = root / "src/core/physics.hpp"
    s = p.read_text()
    marker = "constexpr float HITBOX_LETHAL_SCALE = 0.30f;\n"
    if "INNER_BLOCK_HITBOX_HALF" not in s:
        s = replace_once(
            s, marker,
            marker + "constexpr float INNER_BLOCK_HITBOX_HALF = 4.5f;  // 9x9 block-side hitbox\n",
            "hitbox constant",
        )
    p.write_text(s)

    p = root / "src/core/sim.cpp"
    s = p.read_text()
    old = '''  const float lhw = hw * (1.0f - phys::HITBOX_LETHAL_SCALE);
  const float lhh = hh * (1.0f - phys::HITBOX_LETHAL_SCALE);
'''
    new = '''  const float lhw = hw * (1.0f - phys::HITBOX_LETHAL_SCALE);  // legacy hazard probe
  const float lhh = hh * (1.0f - phys::HITBOX_LETHAL_SCALE);
  const float bhw = phys::INNER_BLOCK_HITBOX_HALF;
  const float bhh = phys::INNER_BLOCK_HITBOX_HALF;
'''
    s = replace_once(s, old, new, "block/hazard hitboxes")
    old = '''    } else if (aabb(st_.x, st_.y, lhw, lhh, o.x, o.y, o.hw, o.hh)) {
      // Ran into the wall for real: that is a death in GD, not a slide.
'''
    new = '''    } else if (aabb(st_.x, st_.y, bhw, bhh, o.x, o.y, o.hw, o.hh)) {
      // Ran into the wall for real: that is a death in GD, not a slide.
'''
    s = replace_once(s, old, new, "solid inner collision")
    p.write_text(s)


def patch_hazard_outer(root: Path) -> None:
    p = root / "src/core/sim.cpp"
    s = p.read_text()
    old = '''    if (aabb(st_.x, st_.y, lhw, lhh, o.x, o.y, o.hw, o.hh)) {
      st_.dead = true;
      return;
    }
'''
    new = '''    if (aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)) {
      st_.dead = true;
      return;
    }
'''
    # First occurrence in pass 2 is the hazard check.
    p.write_text(replace_once(s, old, new, "outer hazard collision"))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("variant", choices=[
        "maps", "cube", "inner", "cube_inner", "combo", "combo_hazard"
    ])
    a = ap.parse_args()
    root = Path(a.root)
    v = a.variant
    if v in {"maps", "combo", "combo_hazard"}:
        patch_mappings(root)
    if v in {"cube", "cube_inner", "combo", "combo_hazard"}:
        patch_cube(root)
    if v in {"inner", "cube_inner", "combo", "combo_hazard"}:
        patch_inner_block(root)
    if v == "combo_hazard":
        patch_hazard_outer(root)
    print("applied", v, "to", root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

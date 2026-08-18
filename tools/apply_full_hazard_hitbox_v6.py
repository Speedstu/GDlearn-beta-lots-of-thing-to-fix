#!/usr/bin/env python3
from pathlib import Path
p=Path('src/core/sim.cpp')
s=p.read_text()
s=s.replace("  const float lhw = hw * (1.0f - phys::HITBOX_LETHAL_SCALE);\n  const float lhh = hh * (1.0f - phys::HITBOX_LETHAL_SCALE);\n  // Solid sides use the compact inner player hitbox.  Hazards keep\n  // their separately tuned lethal box above.\n", "  // Pathfinder uses the full unrotated player hitbox for ordinary hazards.\n  // Solid side collisions still use the compact inner hitbox below.\n")
old="""        : (o.sub == 1
            ? ellipseAabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)
            : aabb(st_.x, st_.y, lhw, lhh, o.x, o.y, o.hw, o.hh));"""
new="""        : (o.sub == 1
            ? ellipseAabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)
            : aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh));"""
if old not in s: raise SystemExit('hazard collision anchor not found')
s=s.replace(old,new,1)
p.write_text(s)
print('full hazard hitbox v6 applied')

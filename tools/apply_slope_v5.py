#!/usr/bin/env python3
from pathlib import Path

# Correct two accidental over-inclusions in the transcribed Pathfinder table.
p=Path('tools/pathfinder_geometry.py'); s=p.read_text()
s=s.replace('[64,195,206,220,661,1155,1156,1157,1208,1910]',
            '[64,195,206,220,661,1155,1157,1208,1910]')
s=s.replace('[40,147,215,369,370,1903,1904,1905]',
            '[40,147,215,369,370,1903,1905]')
p.write_text(s)

# Emit slopes as oriented analytical primitives rather than rectangles.
p=Path('tools/gmd_to_gdl.py'); s=p.read_text()
anchor="""    if oid in pg.SAW_RADII:\n        r = pg.SAW_RADII[oid]\n        return GdlObject('hazard', 1, x, y, r * sx, r * sy, oid)\n"""
insert="""    def slope_orient():\n        q = int(round(rot / 90.0))\n        fx, fy = _i(props, 4, 0) == 1, _i(props, 5, 0) == 1\n        if fx and fy: q += 2\n        elif fx: q += 1\n        elif fy: q += 3\n        return q % 4\n\n    if oid in pg.SLOPE_SOLID_SIZES:\n        w, h = pg.SLOPE_SOLID_SIZES[oid]\n        return GdlObject('solid', 16 + slope_orient(), x, y, 0.5*w*sx, 0.5*h*sy, oid)\n    if oid in pg.SLOPE_HAZARD_SIZES:\n        w, h = pg.SLOPE_HAZARD_SIZES[oid]\n        return GdlObject('hazard', 32 + slope_orient(), x, y, 0.5*w*sx, 0.5*h*sy, oid)\n\n""" + anchor
if anchor not in s: raise SystemExit('converter slope anchor missing')
s=s.replace(anchor,insert,1); p.write_text(s)

# Wire slope collision into the simulator while keeping the .gdl format stable.
p=Path('src/core/sim.cpp'); s=p.read_text()
if '#include "core/slope_collision.hpp"' not in s:
    s=s.replace('#include "core/sim.hpp"\n','#include "core/sim.hpp"\n#include "core/slope_collision.hpp"\n',1)
solid="""    if (o.kind != Kind::Solid) continue;\n    if (!aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)) continue;\n"""
solid2="""    if (o.kind != Kind::Solid) continue;\n    if (slope::solidSlope(o)) {\n      const bool touched = slope::resolveSolid(st_, o, prevY);\n      if (touched) {\n        lastContactUid_ = o.uid;\n        if (st_.dead) { deathUid_ = o.uid; return; }\n        if (st_.onGround) landed = true;\n      }\n      continue;\n    }\n    if (!aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)) continue;\n"""
if solid not in s: raise SystemExit('solid loop anchor missing')
s=s.replace(solid,solid2,1)
haz="""    const bool hit = o.sub == 1\n        ? ellipseAabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)\n        : aabb(st_.x, st_.y, lhw, lhh, o.x, o.y, o.hw, o.hh);\n"""
haz2="""    const bool hit = slope::hazardSlope(o)\n        ? slope::hazardHit(st_, o)\n        : (o.sub == 1\n            ? ellipseAabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)\n            : aabb(st_.x, st_.y, lhw, lhh, o.x, o.y, o.hw, o.hh));\n"""
if haz not in s: raise SystemExit('hazard loop anchor missing')
s=s.replace(haz,haz2,1)
solidat="""    if (o.kind == Kind::Solid && aabb(x, y, 0.5f, 0.5f, o.x, o.y, o.hw, o.hh))\n      return true;\n"""
solidat2="""    if (o.kind != Kind::Solid) continue;\n    if (slope::solidSlope(o)) { if (slope::pointInsideSolid(o, x, y)) return true; }\n    else if (aabb(x, y, 0.5f, 0.5f, o.x, o.y, o.hw, o.hh)) return true;\n"""
if solidat not in s: raise SystemExit('solidAt anchor missing')
s=s.replace(solidat,solidat2,1)
hazat="""    if (o.kind != Kind::Hazard) continue;\n    if (o.sub == 1) {\n      const float dx=(x-o.x)/std::max(0.001f,o.hw);\n      const float dy=(y-o.y)/std::max(0.001f,o.hh);\n      if (dx*dx+dy*dy <= 1.0f) return true;\n    } else if (aabb(x,y,0.5f,0.5f,o.x,o.y,o.hw,o.hh)) return true;\n"""
hazat2="""    if (o.kind != Kind::Hazard) continue;\n    if (slope::hazardSlope(o)) { if (slope::pointInsideHazard(o, x, y)) return true; }\n    else if (o.sub == 1) {\n      const float dx=(x-o.x)/std::max(0.001f,o.hw);\n      const float dy=(y-o.y)/std::max(0.001f,o.hh);\n      if (dx*dx+dy*dy <= 1.0f) return true;\n    } else if (aabb(x,y,0.5f,0.5f,o.x,o.y,o.hw,o.hh)) return true;\n"""
if hazat not in s: raise SystemExit('hazardAt anchor missing')
s=s.replace(hazat,hazat2,1); p.write_text(s)
print('slope v5 patch applied')

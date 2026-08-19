#!/usr/bin/env python3
from pathlib import Path


def rep(path, old, new):
    p=Path(path); s=p.read_text()
    if old not in s:
        raise SystemExit(f'anchor missing in {path}: {old[:80]!r}')
    p.write_text(s.replace(old,new,1))

# Object keeps the original GD rotation (degrees). Existing aggregate/default
# construction remains source-compatible because it has a default value.
rep('src/core/level.hpp',
'''  float x = 0, y = 0;         // centre, units\n  float hw = 15, hh = 15;     // half extents after scale/rotation\n  Kind kind = Kind::Solid;''',
'''  float x = 0, y = 0;         // centre, units\n  float hw = 15, hh = 15;     // local half extents after scale\n  float rotation = 0;         // degrees; 0 for legacy .gdl objects\n  Kind kind = Kind::Solid;''')

# Backward-compatible optional rotation field in .gdl.
rep('src/core/level.cpp',
'''      int sub = 0, id = 0;\n      ss >> kind >> sub >> o.x >> o.y >> o.hw >> o.hh >> id;\n      o.kind = kindFromName(kind);''',
'''      int sub = 0, id = 0;\n      ss >> kind >> sub >> o.x >> o.y >> o.hw >> o.hh >> id;\n      if (!(ss >> o.rotation)) o.rotation = 0.0f;\n      o.kind = kindFromName(kind);''')
rep('src/core/level.cpp',
'''        << static_cast<int>(o.sub) << " " << o.x << " " << o.y << " " << o.hw\n        << " " << o.hh << " " << o.id << "\\n";''',
'''        << static_cast<int>(o.sub) << " " << o.x << " " << o.y << " " << o.hw\n        << " " << o.hh << " " << o.id << " " << o.rotation << "\\n";''')

# Broad phase must use the world-space x radius of a rotated rectangle.
rep('src/core/level.cpp',
'''  float maxX = 0, minX = 0;\n  bool first = true;\n  for (const Object& o : objs_) {\n    float l = o.x - o.hw, r = o.x + o.hw;''',
'''  auto worldXExtent = [](const Object& o) {\n    if (std::fabs(o.rotation) < 1e-6f) return o.hw;\n    const float r = o.rotation * 3.14159265358979323846f / 180.0f;\n    return std::fabs(std::cos(r)) * o.hw + std::fabs(std::sin(r)) * o.hh;\n  };\n  float maxX = 0, minX = 0;\n  bool first = true;\n  for (const Object& o : objs_) {\n    const float ex = worldXExtent(o);\n    float l = o.x - ex, r = o.x + ex;''')
rep('src/core/level.cpp',
'''  auto span = [&](const Object& o, int* lo, int* hi) {\n    // Pad by one bucket on both sides so a query only ever needs ONE bucket.\n    *lo = static_cast<int>((o.x - o.hw - minX_) / kBucket) - 1;\n    *hi = static_cast<int>((o.x + o.hw - minX_) / kBucket) + 1;''',
'''  auto span = [&](const Object& o, int* lo, int* hi) {\n    // Pad by one bucket on both sides so a query only ever needs ONE bucket.\n    const float ex = worldXExtent(o);\n    *lo = static_cast<int>((o.x - ex - minX_) / kBucket) - 1;\n    *hi = static_cast<int>((o.x + ex - minX_) / kBucket) + 1;''')
# level.cpp now needs cmath.
rep('src/core/level.cpp', '#include <cstdlib>\n', '#include <cstdlib>\n#include <cmath>\n')

# Exact OBB-vs-OBB SAT. This mirrors Pathfinder's rotated rectangle intent.
rep('src/core/sim.cpp',
'''inline bool aabb(float ax, float ay, float ahw, float ahh, float bx, float by,\n                 float bhw, float bhh) {\n  return std::fabs(ax - bx) < (ahw + bhw) && std::fabs(ay - by) < (ahh + bhh);\n}\n''',
'''inline bool aabb(float ax, float ay, float ahw, float ahh, float bx, float by,\n                 float bhw, float bhh) {\n  return std::fabs(ax - bx) < (ahw + bhw) && std::fabs(ay - by) < (ahh + bhh);\n}\n\ninline bool obbOverlap(float ax, float ay, float ahw, float ahh, float aDeg,\n                       float bx, float by, float bhw, float bhh, float bDeg) {\n  constexpr float k = 3.14159265358979323846f / 180.0f;\n  const float ar=aDeg*k, br=bDeg*k;\n  const float ac=std::cos(ar), as=std::sin(ar);\n  const float bc=std::cos(br), bs=std::sin(br);\n  const float au[2]={ac,as}, av[2]={-as,ac};\n  const float bu[2]={bc,bs}, bv[2]={-bs,bc};\n  const float d[2]={bx-ax,by-ay};\n  const float axes[4][2]={{au[0],au[1]},{av[0],av[1]},\n                          {bu[0],bu[1]},{bv[0],bv[1]}};\n  auto dot=[](const float* p,const float* q){return p[0]*q[0]+p[1]*q[1];};\n  for (const auto& L: axes) {\n    const float dist=std::fabs(d[0]*L[0]+d[1]*L[1]);\n    const float ra=ahw*std::fabs(dot(au,L))+ahh*std::fabs(dot(av,L));\n    const float rb=bhw*std::fabs(dot(bu,L))+bhh*std::fabs(dot(bv,L));\n    if (dist > ra + rb) return false;\n  }\n  return true;\n}\n''')
rep('src/core/sim.cpp',
'''    if (o.kind == Kind::Portal || o.kind == Kind::Speed) {\n      if (aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)) applyPortal(o);\n    }''',
'''    if (o.kind == Kind::Portal || o.kind == Kind::Speed) {\n      const float mod90 = std::fmod(std::fabs(o.rotation), 90.0f);\n      const bool cardinal = mod90 < 1e-4f || std::fabs(mod90 - 90.0f) < 1e-4f;\n      const float playerRot = cardinal ? 0.0f : st_.rotation;\n      const bool touching = std::fabs(o.rotation) < 1e-6f\n          ? aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)\n          : obbOverlap(st_.x, st_.y, hw, hh, playerRot,\n                       o.x, o.y, o.hw, o.hh, o.rotation);\n      if (touching) applyPortal(o);\n    }''')

# Converter: persist original local half-extents + GD rotation for portals.
rep('tools/gmd_to_gdl.py',
'''    hh: float\n    oid: int\n''',
'''    hh: float\n    oid: int\n    rotation: float = 0.0\n''')
rep('tools/gmd_to_gdl.py',
'''        # Pathfinder/GD rotates the portal collision rectangle with the object.\n        # Keeping 90-degree portals vertical makes their trigger volume far too\n        # tall (ToE2 ID11 at x=2295 fires ~8 ticks early).  The current .gdl\n        # format is axis-aligned, so preserve exact cardinal rotations and use\n        # the conservative rotated AABB for non-cardinal cases until OBB data is\n        # represented explicitly in the core format.\n        pw, ph = _rotated_half_extents(pw * sx, ph * sy, rot)\n        return GdlObject('portal', PORTALS[oid], x, y, pw, ph, oid)''',
'''        # Preserve the local rectangle plus exact GD rotation. Object.cpp in\n        # Pathfinder stores the negated property-6 angle. The C++ simulator now\n        # performs SAT instead of inflating oblique portals into a fake AABB.\n        return GdlObject('portal', PORTALS[oid], x, y, pw * sx, ph * sy, oid, -rot)''')
rep('tools/gmd_to_gdl.py',
'''            f'{o.hw:.6g} {o.hh:.6g} {o.oid}'\n''',
'''            f'{o.hw:.6g} {o.hh:.6g} {o.oid} {o.rotation:.6g}'\n''')
rep('tools/gmd_to_gdl.py',
'''            fh.write('kind,sub,x,y,hw,hh,id\\n')\n            for o in objects:\n                fh.write(f'{o.kind},{o.sub},{o.x},{o.y},{o.hw},{o.hh},{o.oid}\\n')''',
'''            fh.write('kind,sub,x,y,hw,hh,id,rotation\\n')\n            for o in objects:\n                fh.write(f'{o.kind},{o.sub},{o.x},{o.y},{o.hw},{o.hh},{o.oid},{o.rotation}\\n')''')

print('portal OBB patch applied')

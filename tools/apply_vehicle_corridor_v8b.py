#!/usr/bin/env python3
from pathlib import Path

def rep(path, old, new, n=1):
    p=Path(path); s=p.read_text()
    if old not in s: raise SystemExit(f'anchor missing {path}: {old[:120]!r}')
    p.write_text(s.replace(old,new,n))

rep('src/core/sim.hpp',
'''  float rotation = 0;\n  float speed = phys::SPEEDS[1];\n  int32_t frame = 0;''',
'''  float rotation = 0;\n  float speed = phys::SPEEDS[1];\n  float flightFloor = 0.0f;\n  float flightCeiling = 300.0f;\n  int32_t frame = 0;''')

rep('src/search/beam.cpp',
'''  mix(static_cast<uint64_t>(qv + 10000));\n  mix(static_cast<uint64_t>(s.mode));''',
'''  mix(static_cast<uint64_t>(qv + 10000));\n  mix(static_cast<uint64_t>(static_cast<int64_t>(std::lround(s.flightFloor * 2.0f)) + 1000000));\n  mix(static_cast<uint64_t>(static_cast<int64_t>(std::lround(s.flightCeiling * 2.0f)) + 1000000));\n  mix(static_cast<uint64_t>(s.mode));''')

rep('src/app/main.cpp',
'''      sa.x==sb.x && sa.y==sb.y && sa.vy==sb.vy && sa.rotation==sb.rotation &&\n      sa.speed==sb.speed && sa.frame==sb.frame &&''',
'''      sa.x==sb.x && sa.y==sb.y && sa.vy==sb.vy && sa.rotation==sb.rotation &&\n      sa.speed==sb.speed && sa.flightFloor==sb.flightFloor &&\n      sa.flightCeiling==sb.flightCeiling && sa.frame==sb.frame &&''')

old_corridor='''  // Flight ceiling, measured from the surface UNDER the player so elevated\n  // corridors get an elevated ceiling -- this is what GD's camera does. A\n  // static roof in the level file cannot express it: level 1's corridor\n  // ceilings sit at 9.5-11.5 blocks in places and at 0 in others.\n  {\n    float ground = level_->floorY;\n    int gn = 0;\n    const int32_t* gidx = level_->bucketBegin(st_.x, &gn);\n    if (gidx) {\n      const std::vector<Object>& all = level_->objects();\n      for (int i = 0; i < gn; ++i) {\n        const Object& o = all[gidx[i]];\n        if (o.kind != Kind::Solid) continue;\n        if (std::fabs(o.x - st_.x) > o.hw + hw) continue;\n        const float topY = o.y + o.hh;\n        if (topY <= st_.y - hh + 2.0f && topY > ground) ground = topY;\n      }\n    }\n    lastCeiling_ = ground + phys::FLIGHT_CEILING;\n    if (isFlightMode(st_.mode) && st_.y + hh >= lastCeiling_) {\n      st_.y = lastCeiling_ - hh;\n      if (st_.vy > 0) st_.vy = 0;\n      if (diesOnTouch(st_.mode)) st_.dead = true;\n    }\n\n    // No second flipped-gravity clamp is needed here: the dynamic\n    // flight corridor above is defined in world space and already applies to\n    // both gravity directions. A second remote clamp used to teleport inverted\n    // ships below floorY when a nearby solid was less than one corridor-height\n    // above them.\n  }'''
new_corridor='''  // Geometry Dash stores a world-space vehicle corridor on vehicle-portal\n  // contact. It is NOT inferred from nearby solids. Gravity only chooses which\n  // side is the active ceiling.\n  if (isFlightMode(st_.mode)) {\n    const float extent = st_.mode == Mode::Wave ? 2.0f * hh : hh;\n    lastCeiling_ = st_.flip ? st_.flightFloor : st_.flightCeiling;\n    if (!st_.flip) {\n      if (st_.y + extent >= st_.flightCeiling) {\n        st_.y = st_.flightCeiling - extent;\n        if (st_.vy > 0.0f) st_.vy = 0.0f;\n        if (diesOnTouch(st_.mode)) st_.dead = true;\n      }\n    } else {\n      if (st_.y - extent <= st_.flightFloor) {\n        st_.y = st_.flightFloor + extent;\n        if (st_.vy < 0.0f) st_.vy = 0.0f;\n        if (diesOnTouch(st_.mode)) st_.dead = true;\n      }\n    }\n  }'''
rep('src/core/sim.cpp', old_corridor, new_corridor)

rep('src/core/sim.cpp',
'''    case Mode::Wave: {\n      const float slope = st_.mini ? phys::WAVE_SLOPE_MINI : phys::WAVE_SLOPE;\n      st_.vy = (hold ? 1.0f : -1.0f) * g * slope * st_.speed * phys::DT;\n      break;\n    }''',
'''    case Mode::Wave: {\n      const float slope = st_.mini ? phys::WAVE_SLOPE_MINI : phys::WAVE_SLOPE;\n      float nextVy = (hold ? 1.0f : -1.0f) * g * slope * st_.speed * phys::DT;\n      const float extent = 2.0f * st_.halfH();\n      if (st_.y + extent >= st_.flightCeiling - 1e-5f && nextVy > 0.0f) nextVy = 0.0f;\n      if (st_.y - extent <= st_.flightFloor + 1e-5f && nextVy < 0.0f) nextVy = 0.0f;\n      st_.vy = nextVy;\n      break;\n    }''')

rep('src/core/sim.cpp',
'''void Sim::applyPortal(const Object& o) {''',
'''inline float vehicleBounds(Mode m) {\n  switch (m) {\n    case Mode::Ship: return 300.0f;\n    case Mode::Ball: return 240.0f;\n    case Mode::Ufo:  return 300.0f;\n    case Mode::Wave: return 300.0f;\n    default: return 0.0f;\n  }\n}\n\nvoid Sim::applyPortal(const Object& o) {''')

rep('src/core/sim.cpp',
'''  switch (static_cast<PortalKind>(o.sub)) {\n    case PortalKind::ModeCube: {''',
'''  const PortalKind pk = static_cast<PortalKind>(o.sub);\n  switch (pk) {\n    case PortalKind::ModeCube: {''')

rep('src/core/sim.cpp',
'''    case PortalKind::SizeNormal: st_.mini = false; break;\n    case PortalKind::SizeMini: st_.mini = true; break;\n  }\n}''',
'''    case PortalKind::SizeNormal: st_.mini = false; break;\n    case PortalKind::SizeMini: st_.mini = true; break;\n  }\n\n  const bool vehiclePortal = pk == PortalKind::ModeCube || pk == PortalKind::ModeShip ||\n      pk == PortalKind::ModeBall || pk == PortalKind::ModeUfo || pk == PortalKind::ModeWave ||\n      pk == PortalKind::ModeRobot || pk == PortalKind::ModeSpider || pk == PortalKind::ModeSwing;\n  if (vehiclePortal) {\n    const float bounds = vehicleBounds(st_.mode);\n    if (bounds > 0.0f) {\n      const float raw = (o.y - (bounds * 0.5f + 30.0f)) / 30.0f;\n      st_.flightFloor = std::max(0.0f, 30.0f * std::ceil(raw));\n      st_.flightCeiling = st_.flightFloor + bounds;\n    } else {\n      st_.flightFloor = level_ ? level_->floorY : 0.0f;\n      st_.flightCeiling = 1.0e9f;\n    }\n  }\n}''')

print('vehicle corridor v8b applied')

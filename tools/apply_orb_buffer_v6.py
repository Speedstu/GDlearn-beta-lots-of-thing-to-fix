#!/usr/bin/env python3
from pathlib import Path

# State: one persistent input-buffer bit. The previous-frame state is passed
# locally through step(), so no duplicate previous-position fields are needed.
p=Path('src/core/sim.hpp'); s=p.read_text()
s=s.replace('''  bool holding = false;\n  bool dead = false;\n''','''  bool holding = false;\n  bool buffer = false;\n  bool dead = false;\n''',1)
s=s.replace('''  void resolveWorld(float prevY);\n''','''  void resolveWorld(const State& prev);\n''',1)
p.write_text(s)

p=Path('src/core/sim.cpp'); s=p.read_text()
old='''  const bool press = hold && !st_.holding;\n  st_.holding = hold;\n  st_.holdFrames = hold ? static_cast<uint16_t>(st_.holdFrames + 1) : 0;\n\n  const float prevY = st_.y;\n  st_.x += st_.speed * phys::DT;\n  st_.y += st_.vy;\n  resolveWorld(prevY);\n'''
new='''  const State prev = st_;\n  const bool press = hold && !prev.holding;\n  // Pathfinder/GD input buffering: only a button edge writes the buffer.\n  // A held press stays buffered until an orb consumes it.\n  if (hold != prev.holding) st_.buffer = hold;\n  st_.holding = hold;\n  st_.holdFrames = hold ? static_cast<uint16_t>(st_.holdFrames + 1) : 0;\n\n  st_.x += st_.speed * phys::DT;\n  st_.y += st_.vy;\n  resolveWorld(prev);\n'''
if old not in s: raise SystemExit('step input anchor missing')
s=s.replace(old,new,1)
s=s.replace('''void Sim::resolveWorld(float prevY) {\n''','''void Sim::resolveWorld(const State& prev) {\n''',1)
# Keep existing block formulas using prevY with a local alias.
s=s.replace('''  if (!level_) return;\n  const float g = st_.gdir();\n''','''  if (!level_) return;\n  const float prevY = prev.y;\n  const float g = st_.gdir();\n''',1)
old='''    } else if (o.kind == Kind::Orb) {\n      if (st_.holding && st_.holdFrames == 1 && o.uid != st_.lastOrbUid &&\n          aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh))\n        applyOrb(o);\n    }\n'''
new='''    } else if (o.kind == Kind::Orb) {\n      const bool touchingNow = aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh);\n      const bool touchingPrev = aabb(prev.x, prev.y, prev.halfW(), prev.halfH(),\n                                     o.x, o.y, o.hw, o.hh);\n      // Orb::touching in Pathfinder is current OR previous frame.  The second\n      // term reproduces release-coyote: a buffered press from the previous\n      // frame may still fire while the button is now up.\n      const bool canFire = st_.buffer || (prev.buffer && !st_.holding);\n      if (canFire && o.uid != st_.lastOrbUid && (touchingNow || touchingPrev))\n        applyOrb(o);\n    }\n'''
if old not in s: raise SystemExit('orb collision anchor missing')
s=s.replace(old,new,1)
# Orb consumes buffer exactly once.
s=s.replace('''  st_.lastOrbUid = o.uid;\n  const OrbKind k = static_cast<OrbKind>(o.sub);\n''','''  st_.lastOrbUid = o.uid;\n  st_.buffer = false;\n  const OrbKind k = static_cast<OrbKind>(o.sub);\n''',1)
p.write_text(s)

# Beam dedup must preserve semantically different buffered states.
p=Path('src/search/beam.cpp'); s=p.read_text()
old='''  mix((s.flip ? 1u : 0u) | (s.mini ? 2u : 0u) | (s.onGround ? 4u : 0u) |\n      (s.holding ? 8u : 0u));\n'''
new='''  mix((s.flip ? 1u : 0u) | (s.mini ? 2u : 0u) | (s.onGround ? 4u : 0u) |\n      (s.holding ? 8u : 0u) | (s.buffer ? 16u : 0u));\n'''
if old not in s: raise SystemExit('beam state key anchor missing')
p.write_text(s.replace(old,new,1))

# Use one of the spare scalar slots without changing obs_dim/checkpoint schema.
p=Path('src/env/obs.cpp'); s=p.read_text()
old='''    sc[i++] = fy;\n  }\n  while (i < ObsSpec::kScalars) sc[i++] = 0.0f;\n'''
new='''    sc[i++] = fy;\n  }\n  if (i < ObsSpec::kScalars) sc[i++] = s.buffer ? 1.0f : 0.0f;\n  while (i < ObsSpec::kScalars) sc[i++] = 0.0f;\n'''
if old not in s: raise SystemExit('obs spare scalar anchor missing')
p.write_text(s.replace(old,new,1))
print('orb buffer/coyote v6 applied')

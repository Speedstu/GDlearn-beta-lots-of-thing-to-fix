#!/usr/bin/env python3
from pathlib import Path

p=Path('src/core/sim.cpp')
s=p.read_text()
old='''    if (o.kind != Kind::Solid) continue;\n    if (slope::solidSlope(o)) {\n      const bool touched = slope::resolveSolid(st_, o, prevY);\n      if (touched) {\n        lastContactUid_ = o.uid;\n        if (st_.dead) { deathUid_ = o.uid; return; }\n        if (st_.onGround) landed = true;\n      }\n      continue;\n    }\n    if (!aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)) continue;\n\n    if (diesOnTouch(st_.mode)) {\n'''
new='''    if (o.kind != Kind::Solid) continue;\n    if (slope::solidSlope(o)) {\n      const bool touched = slope::resolveSolid(st_, o, prevY);\n      if (touched) {\n        lastContactUid_ = o.uid;\n        if (st_.dead) { deathUid_ = o.uid; return; }\n        if (st_.onGround) landed = true;\n      }\n      continue;\n    }\n\n    const float surfaceTop = o.y + o.hh * g;      // support face in gravity space\n    const float surfaceBottom = o.y - o.hh * g;   // opposite face\n    const float prevFeet = prevY - hh * g;\n    const float prevHead = prevY + hh * g;\n    const bool falling = st_.vy * g <= 0.0f;\n\n    // Pathfinder's Block::collide keeps a cube attached to a support surface\n    // through a vertical `clip` tolerance of 10 world units.  The old gdlearn\n    // broad-phase required strict outer-AABB overlap, so a cube exactly resting\n    // on a block alternated grounded/airborne every native tick.\n    const bool horizontalSupport =\n        std::fabs(st_.x - o.x) <= (hw + o.hw);\n    const float supportGap = ((st_.y - hh * g) - surfaceTop) * g;\n    if (prev.onGround && falling && horizontalSupport &&\n        supportGap >= -1.0f && supportGap <= 10.0f) {\n      st_.y = surfaceTop + hh * g;\n      st_.vy = 0.0f;\n      st_.onGround = true;\n      st_.jumpHold = 0;\n      lastContactUid_ = o.uid;\n      landed = true;\n      continue;\n    }\n\n    if (!aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)) continue;\n\n    if (diesOnTouch(st_.mode)) {\n'''
if old not in s:
    raise SystemExit('solid-loop anchor missing')
s=s.replace(old,new,1)
# Remove duplicate declarations now emitted before the broad-phase.
dup='''    const float surfaceTop = o.y + o.hh * g;      // "floor" side in grav space\n    const float surfaceBottom = o.y - o.hh * g;   // "ceiling" side\n    const float prevFeet = prevY - hh * g;\n    const float prevHead = prevY + hh * g;\n    const bool falling = st_.vy * g <= 0.0f;\n\n'''
if dup not in s:
    raise SystemExit('duplicate contact declarations missing')
s=s.replace(dup,'',1)
p.write_text(s)
print('ground support v6 applied')

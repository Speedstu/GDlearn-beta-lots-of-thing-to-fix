#!/usr/bin/env python3
from pathlib import Path

p=Path('src/core/sim.cpp')
s=p.read_text()
old='''  resolveWorld(prev);\n  if (alive()) applyMotion(press, hold);\n'''
new='''  resolveWorld(prev);\n\n  // Pathfinder/Geometry Dash has a deliberate edge-case when walking off an\n  // elevated block: the player falls one native frame faster than the naive\n  // integration predicts.  postCollision() adds one rounded previous-frame\n  // acceleration displacement and, when velocity was zero, one extra rounded\n  // acceleration contribution before the regular vehicle update.\n  if (alive() && prev.mode == Mode::Cube && !prev.flip && prev.onGround &&\n      !st_.onGround && (!hold || st_.buffer) && level_ &&\n      (prev.y - prev.halfH()) > level_->floorY + 0.01f) {\n    const int tier = std::clamp<int>(prev.tier, 0, 4);\n    const float accelTick =\n        -(phys::CUBE_ACCEL_U_S2[tier] / (phys::TPS * phys::TPS));\n    const float roundedAccelTick = roundWorldVy(accelTick, prev.flip);\n    st_.y += roundedAccelTick;\n    if (std::fabs(st_.vy) < 1e-7f) st_.vy += roundedAccelTick;\n  }\n\n  if (alive()) applyMotion(press, hold);\n'''
if old not in s:
    raise SystemExit('step post-collision anchor missing')
p.write_text(s.replace(old,new,1))
print('fast ledge fall v6 applied')

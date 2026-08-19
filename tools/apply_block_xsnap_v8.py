#!/usr/bin/env python3
from pathlib import Path

def rep(path,old,new,n=1):
    p=Path(path); s=p.read_text()
    if old not in s: raise SystemExit(f'anchor missing {path}: {old[:90]!r}')
    p.write_text(s.replace(old,new,n))

# Snapshot state: enough to reproduce Pathfinder Block::trySnap without a path-global history.
rep('src/core/sim.hpp',
'''  int32_t lastPadUid = -1;\n  uint16_t holdFrames = 0;''',
'''  int32_t lastPadUid = -1;\n  int32_t snapUid = -1;        // previous cube support block for staircase X-snap\n  int32_t snapFrame = -1;      // tick when snapUid became the support\n  float snapNextX = 0.0f;      // x of the next player state from snapFrame\n  uint16_t holdFrames = 0;''')

# Search key must preserve future snap behaviour.
rep('src/search/beam.cpp',
'''  mix(static_cast<uint64_t>(s.lastPadUid + 2));\n  return h;''',
'''  mix(static_cast<uint64_t>(s.lastPadUid + 2));\n  mix(static_cast<uint64_t>(s.snapUid + 2));\n  mix(static_cast<uint64_t>(s.snapFrame + 2));\n  mix(static_cast<uint64_t>(static_cast<int64_t>(std::lround(s.snapNextX * 2.0f)) + 1000000));\n  return h;''')

# Pathfinder snapThreshold table.
needle='''inline float cubeOrbUS(OrbKind k, int tier, bool mini) {'''
helper='''inline float cubeSnapThreshold(float dx, float dy, int tier, bool mini) {\n  float stairs[3][2] = {};\n  float threshold = 1.0f;\n  switch (tier) {\n    case 0:\n      stairs[0][0]=120; stairs[0][1]=-30; stairs[1][0]=90; stairs[1][1]=30; stairs[2][0]=60; stairs[2][1]=60; threshold=1; break;\n    case 1:\n      stairs[0][0]=150; stairs[0][1]=-30; stairs[1][0]=mini?90:120; stairs[1][1]=30; stairs[2][0]=90; stairs[2][1]=60; threshold=1; break;\n    case 2:\n      stairs[0][0]=180; stairs[0][1]=-30; stairs[1][0]=mini?90:150; stairs[1][1]=30; stairs[2][0]=120; stairs[2][1]=60; threshold=2; break;\n    case 3:\n      stairs[0][0]=225; stairs[0][1]=-30; stairs[1][0]=mini?90:180; stairs[1][1]=30; stairs[2][0]=135; stairs[2][1]=60; threshold=2; break;\n    default:\n      stairs[0][0]=150; stairs[0][1]=-30; stairs[1][0]=120; stairs[1][1]=30; stairs[2][0]=90; stairs[2][1]=60; threshold=1; break;\n  }\n  for (const auto& st : stairs)\n    if (std::fabs(dx-st[0]) <= threshold && std::fabs(dy-st[1]) <= threshold) return threshold;\n  return 0.0f;\n}\n\n'''+needle
rep('src/core/sim.cpp',needle,helper)

# Local lambda inside resolveWorld after objs is available.
anchor='''  const std::vector<Object>& objs = level_->objects();\n\n  // Pass 1: portals and speed changes.'''
insert='''  const std::vector<Object>& objs = level_->objects();\n\n  auto updateCubeSnap = [&](const Object& support, const State& before) {\n    if (st_.mode != Mode::Cube) return;\n    if (!before.onGround && st_.snapUid >= 0 && st_.snapUid < static_cast<int32_t>(objs.size()) &&\n        st_.snapFrame > 0 && st_.snapFrame + 1 < st_.frame) {\n      const Object& previous = objs[static_cast<size_t>(st_.snapUid)];\n      const float dx = support.x - previous.x;\n      const float dy = (support.y - previous.y) * st_.gdir();\n      const float threshold = cubeSnapThreshold(dx, dy, st_.tier, st_.mini);\n      if (threshold > 0.0f) {\n        const float target = st_.snapNextX + dx;\n        st_.x = std::clamp(target, st_.x - threshold, st_.x + threshold);\n      }\n    }\n    st_.snapUid = support.uid;\n    st_.snapFrame = st_.frame;\n    // Pathfinder stores getState(frame).nextPlayer()->pos.x. Horizontal motion\n    // happens at the beginning of that next frame, before block collision.\n    st_.snapNextX = st_.x + st_.speed * phys::DT;\n  };\n\n  // Pass 1: portals and speed changes.'''
rep('src/core/sim.cpp',anchor,insert)

# Apply after each successful top-face landing/corner snap. There are two exact blocks.
old='''      lastContactUid_ = o.uid;\n      landed = true;\n    } else if (!falling'''
new='''      lastContactUid_ = o.uid;\n      landed = true;\n      updateCubeSnap(o, prev);\n    } else if (!falling'''
rep('src/core/sim.cpp',old,new,1)
old2='''        lastContactUid_ = o.uid;\n        landed = true;\n      }\n    }\n  }'''
new2='''        lastContactUid_ = o.uid;\n        landed = true;\n        updateCubeSnap(o, prev);\n      }\n    }\n  }'''
rep('src/core/sim.cpp',old2,new2,1)

# Determinism selftest compares all gameplay state fields.
rep('src/app/main.cpp',
'''      sa.speed==sb.speed && sa.frame==sb.frame && sa.lastOrbUid==sb.lastOrbUid &&\n      sa.lastPadUid==sb.lastPadUid && sa.holdFrames==sb.holdFrames && sa.jumpHold==sb.jumpHold &&''',
'''      sa.speed==sb.speed && sa.frame==sb.frame && sa.lastOrbUid==sb.lastOrbUid &&\n      sa.lastPadUid==sb.lastPadUid && sa.snapUid==sb.snapUid && sa.snapFrame==sb.snapFrame &&\n      sa.snapNextX==sb.snapNextX && sa.holdFrames==sb.holdFrames && sa.jumpHold==sb.jumpHold &&''')
print('block X-snap patch applied')

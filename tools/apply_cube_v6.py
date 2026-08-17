#!/usr/bin/env python3
from pathlib import Path

p=Path('src/core/physics.hpp');s=p.read_text()
a='constexpr float CUBE_TERMINAL = 15.0f * VEL_60_TO_TICK;\n'
b='''constexpr float CUBE_TERMINAL = 810.0f / TPS;\nconstexpr std::array<float, 5> CUBE_ACCEL_U_S2 = {2747.52f,2794.1082f,2786.4f,2799.36f,2799.36f};\nconstexpr std::array<float, 5> CUBE_JUMP_U_S = {573.481728f,603.7217172f,616.681728f,606.421728f,606.421728f};\n'''
if a not in s: raise SystemExit('cube terminal anchor')
p.write_text(s.replace(a,b,1))

p=Path('src/core/sim.cpp');s=p.read_text()
a='''inline bool isFlightMode(Mode m) {\n  return m == Mode::Ship || m == Mode::Ufo || m == Mode::Wave || m == Mode::Swing;\n}\n'''
b=a+'''\ninline float roundWorldVy(float worldVy, bool flip) {\n  double rel = static_cast<double>(worldVy) * (flip ? -1.0 : 1.0) * phys::TPS;\n  const double sign = flip ? 1.0 : -1.0;\n  double n = rel / 54.0 * sign;\n  const double truncated = static_cast<int>(n);\n  if (n != truncated) n = std::round((n - truncated) * 1000.0) / 1000.0 + truncated;\n  rel = n * 54.0 * sign;\n  return static_cast<float>(rel * (flip ? -1.0 : 1.0) / phys::TPS);\n}\n'''
if a not in s: raise SystemExit('helper anchor')
s=s.replace(a,b,1)
a='''  const float prevY = st_.y;\n  applyMotion(press, hold);\n  st_.x += st_.speed * phys::DT;\n  resolveWorld(prevY);\n'''
b='''  const float prevY = st_.y;\n  st_.x += st_.speed * phys::DT;\n  st_.y += st_.vy;\n  resolveWorld(prevY);\n  if (alive()) applyMotion(press, hold);\n'''
if a not in s: raise SystemExit('step order anchor')
s=s.replace(a,b,1)
a='''    case Mode::Cube: {\n      // Real GD buffers the button: holding makes the cube jump on every\n      // ground contact, no release required. The old sim demanded a fresh\n      // press, which made whole classes of jumps unreachable.\n      if (hold && st_.onGround) {\n        st_.vy = phys::CUBE_JUMP * g;\n        st_.onGround = false;\n      }\n      st_.vy -= phys::CUBE_GRAVITY * g;\n      clampFall(phys::CUBE_TERMINAL);\n      break;\n    }\n'''
b='''    case Mode::Cube: {\n      const int tier = std::clamp<int>(st_.tier, 0, 4);\n      bool velocityOverride = false;\n      if (st_.onGround) {\n        if (hold) {\n          const float rel = phys::CUBE_JUMP_U_S[tier] * (st_.mini ? 0.8f : 1.0f);\n          st_.vy = (rel / phys::TPS) * g;\n          st_.onGround = false;\n          velocityOverride = !press;\n        } else { st_.vy = 0.0f; velocityOverride = true; }\n      }\n      if (!velocityOverride) st_.vy -= (phys::CUBE_ACCEL_U_S2[tier] / (phys::TPS * phys::TPS)) * g;\n      if (st_.vy * g < -phys::CUBE_TERMINAL) st_.vy = -phys::CUBE_TERMINAL * g;\n      st_.vy = roundWorldVy(st_.vy, st_.flip);\n      break;\n    }\n'''
if a not in s: raise SystemExit('cube anchor')
s=s.replace(a,b,1)
a='''  st_.y += st_.vy;\n}\n\n// --------------------------------------------------------- world contact ----\n'''
if a not in s: raise SystemExit('motion tail')
s=s.replace(a,'''}\n\n// --------------------------------------------------------- world contact ----\n''',1)
p.write_text(s)
print('cube v6 patch applied')

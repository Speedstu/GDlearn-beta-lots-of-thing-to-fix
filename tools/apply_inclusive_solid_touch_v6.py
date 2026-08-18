#!/usr/bin/env python3
from pathlib import Path

p = Path('src/core/sim.cpp')
s = p.read_text()

anchor = '''inline bool aabb(float ax, float ay, float ahw, float ahh, float bx, float by,
                 float bhw, float bhh) {
  return std::fabs(ax - bx) < (ahw + bhw) && std::fabs(ay - by) < (ahh + bhh);
}
'''
replacement = anchor + '''
// Pathfinder Entity::intersects treats edge contact as an intersection.  Keep
// the strict helper for effects/hazards, but use this inclusive version for
// block contact so a cube resting exactly on a surface remains grounded.
inline bool aabbTouching(float ax, float ay, float ahw, float ahh,
                         float bx, float by, float bhw, float bhh) {
  return std::fabs(ax - bx) <= (ahw + bhw) &&
         std::fabs(ay - by) <= (ahh + bhh);
}
'''
if anchor not in s:
    raise SystemExit('aabb helper anchor not found')
s = s.replace(anchor, replacement, 1)

old_support = '''    // Pathfinder's Block::collide keeps a cube attached to a support surface
    // through a vertical `clip` tolerance of 10 world units.  The old gdlearn
    // broad-phase required strict outer-AABB overlap, so a cube exactly resting
    // on a block alternated grounded/airborne every native tick.
    const bool horizontalSupport =
        std::fabs(st_.x - o.x) <= (hw + o.hw);
    const float supportGap = ((st_.y - hh * g) - surfaceTop) * g;
    if (prev.onGround && falling && horizontalSupport &&
        supportGap >= -1.0f && supportGap <= 10.0f) {
      st_.y = surfaceTop + hh * g;
      st_.vy = 0.0f;
      st_.onGround = true;
      st_.jumpHold = 0;
      lastContactUid_ = o.uid;
      landed = true;
      continue;
    }

    if (!aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)) continue;
'''
new_support = '''    // Geometry Dash only calls Block::collide after Object::touching succeeds.
    // Its rectangle intersection is inclusive, so exact edge contact counts,
    // but a block must never pull the player across a free vertical gap.
    if (!aabbTouching(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)) continue;
'''
if old_support not in s:
    raise SystemExit('legacy support block anchor not found')
s = s.replace(old_support, new_support, 1)

old_inner = '    } else if (aabb(st_.x, st_.y, bhw, bhh, o.x, o.y, o.hw, o.hh)) {'
new_inner = '    } else if (aabbTouching(st_.x, st_.y, bhw, bhh, o.x, o.y, o.hw, o.hh)) {'
if old_inner not in s:
    raise SystemExit('inner block hitbox anchor not found')
s = s.replace(old_inner, new_inner, 1)

p.write_text(s)
print('inclusive solid touching v6 applied')

// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>

#include "core/sim.hpp"

namespace gd::slope {

inline bool solidSlope(const Object& o) {
  return o.kind == Kind::Solid && o.sub >= 16 && o.sub < 20;
}
inline bool hazardSlope(const Object& o) {
  return o.kind == Kind::Hazard && o.sub >= 32 && o.sub < 36;
}
inline int orientation(const Object& o) {
  return solidSlope(o) ? static_cast<int>(o.sub) - 16
                       : static_cast<int>(o.sub) - 32;
}
inline int gravityOrientation(int orient, bool upsideDown) {
  if (!upsideDown) return orient;
  switch (orient) {
    case 3: return 0;
    case 2: return 1;
    case 0: return 3;
    case 1: return 2;
    default: return orient;
  }
}
inline float lineY(const Object& o, float x) {
  const float left = o.x - o.hw;
  const float width = std::max(0.001f, 2.0f * o.hw);
  const float t = std::clamp((x - left) / width, 0.0f, 1.0f);
  const int q = orientation(o);
  if (q == 0 || q == 2)
    return (o.y - o.hh) + t * (2.0f * o.hh);
  return (o.y + o.hh) - t * (2.0f * o.hh);
}
inline float expectedCenterY(const Object& o, float playerHalfH, float x) {
  const float w = std::max(0.001f, 2.0f * o.hw);
  const float h = 2.0f * o.hh;
  const float ang = std::atan2(h, w);
  const float offset = playerHalfH / std::max(0.1f, std::cos(ang));
  return lineY(o, x) + (orientation(o) < 2 ? offset : -offset);
}
inline bool pointInsideSolid(const Object& o, float x, float y) {
  if (!solidSlope(o) || x < o.x - o.hw || x > o.x + o.hw ||
      y < o.y - o.hh || y > o.y + o.hh) return false;
  const float line = lineY(o, x);
  return orientation(o) < 2 ? y <= line : y >= line;
}
inline bool pointInsideHazard(const Object& o, float x, float y) {
  if (!hazardSlope(o) || x < o.x - o.hw || x > o.x + o.hw ||
      y < o.y - o.hh || y > o.y + o.hh) return false;
  const float line = lineY(o, x);
  return orientation(o) < 2 ? y <= line : y >= line;
}
inline bool broadOverlap(const State& s, const Object& o) {
  return std::fabs(s.x - o.x) < s.halfW() + o.hw &&
         std::fabs(s.y - o.y) < s.halfH() + o.hh + 8.0f;
}

// Lightweight deterministic port of Pathfinder's slope contact geometry.
// It deliberately omits the long-history ejection heuristic; horizontal GD
// speed is fixed here, so snapping to the analytical surface plus the current
// vertical velocity is substantially closer than the old 30x30 AABB wall.
inline bool resolveSolid(State& s, const Object& o, float prevY) {
  if (!solidSlope(o) || !broadOverlap(s, o)) return false;
  if (s.mode == Mode::Wave) { s.dead = true; return true; }

  const int go = gravityOrientation(orientation(o), s.flip);
  const bool groundFacing = go < 2;
  const float expected = expectedCenterY(o, s.halfH(), s.x);
  const float prevExpected = expectedCenterY(o, s.halfH(), s.x - s.speed * phys::DT);
  const float g = s.gdir();

  if (groundFacing && s.vy * g <= 0.0f) {
    // Crossed into the surface this tick, or remained within the small coyote
    // band used by GD when following a slope.
    const float before = (prevY - prevExpected) * g;
    const float now = (s.y - expected) * g;
    if (before >= -3.0f && now <= 4.0f) {
      s.y = expected;
      if (s.vy * g < 0.0f) s.vy = 0.0f;
      s.onGround = true;
      s.jumpHold = 0;
      return true;
    }
  }

  // Wrong-face penetration is lethal for ground vehicles. Check the compact
  // inner hitbox by sampling its centre and four corners against the triangle.
  const float iw = s.halfW() * phys::HITBOX_LETHAL_SCALE;
  const float ih = s.halfH() * phys::HITBOX_LETHAL_SCALE;
  const float xs[3] = {s.x, s.x - iw, s.x + iw};
  const float ys[3] = {s.y, s.y - ih, s.y + ih};
  for (float x : xs) for (float y : ys) {
    if (pointInsideSolid(o, x, y)) { s.dead = true; return true; }
  }
  return true;
}

inline bool hazardHit(const State& s, const Object& o) {
  if (!hazardSlope(o) || !broadOverlap(s, o)) return false;
  // Pathfinder makes hazardous slopes slightly thicker. Sample the centre and
  // outer corners against the dangerous triangle plus a 4-unit normal margin.
  const float xs[3] = {s.x, s.x - s.halfW(), s.x + s.halfW()};
  const float ys[3] = {s.y, s.y - s.halfH(), s.y + s.halfH()};
  for (float x : xs) for (float y : ys) {
    if (x < o.x - o.hw || x > o.x + o.hw) continue;
    const float e = expectedCenterY(o, 4.0f, x);
    if (orientation(o) < 2 ? y < e : y > e) return true;
  }
  return false;
}

}  // namespace gd::slope

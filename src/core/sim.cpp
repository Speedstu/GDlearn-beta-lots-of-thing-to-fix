// SPDX-License-Identifier: MIT
#include "core/sim.hpp"
#include "core/slope_collision.hpp"

#include <algorithm>
#include <cmath>

namespace gd {
namespace {

inline bool aabb(float ax, float ay, float ahw, float ahh, float bx, float by,
                 float bhw, float bhh) {
  return std::fabs(ax - bx) < (ahw + bhw) && std::fabs(ay - by) < (ahh + bhh);
}

inline bool obbOverlap(float ax, float ay, float ahw, float ahh, float aDeg,
                       float bx, float by, float bhw, float bhh, float bDeg) {
  constexpr float k = 3.14159265358979323846f / 180.0f;
  const float ar=aDeg*k, br=bDeg*k;
  const float ac=std::cos(ar), as=std::sin(ar);
  const float bc=std::cos(br), bs=std::sin(br);
  const float au[2]={ac,as}, av[2]={-as,ac};
  const float bu[2]={bc,bs}, bv[2]={-bs,bc};
  const float d[2]={bx-ax,by-ay};
  const float axes[4][2]={{au[0],au[1]},{av[0],av[1]},
                          {bu[0],bu[1]},{bv[0],bv[1]}};
  auto dot=[](const float* p,const float* q){return p[0]*q[0]+p[1]*q[1];};
  for (const auto& L: axes) {
    const float dist=std::fabs(d[0]*L[0]+d[1]*L[1]);
    const float ra=ahw*std::fabs(dot(au,L))+ahh*std::fabs(dot(av,L));
    const float rb=bhw*std::fabs(dot(bu,L))+bhh*std::fabs(dot(bv,L));
    if (dist > ra + rb) return false;
  }
  return true;
}

// Pathfinder Entity::intersects treats edge contact as an intersection. Keep
// the strict helper for effects/hazards, but use this inclusive version for
// block contact so a cube resting exactly on a surface remains grounded.
inline bool aabbTouching(float ax, float ay, float ahw, float ahh,
                         float bx, float by, float bhw, float bhh) {
  return std::fabs(ax - bx) <= (ahw + bhw) &&
         std::fabs(ay - by) <= (ahh + bhh);
}

inline bool ellipseAabb(float ax, float ay, float ahw, float ahh,
                        float ex, float ey, float rx, float ry) {
  if (rx <= 0.0f || ry <= 0.0f) return false;
  const float qx = std::clamp(ex, ax - ahw, ax + ahw);
  const float qy = std::clamp(ey, ay - ahh, ay + ahh);
  const float dx = (qx - ex) / rx, dy = (qy - ey) / ry;
  return dx * dx + dy * dy <= 1.0f;
}

// Modes that die on any solid contact instead of landing on it.
inline bool diesOnTouch(Mode m) { return m == Mode::Wave; }

inline bool isFlightMode(Mode m) {
  return m == Mode::Ship || m == Mode::Ufo || m == Mode::Wave || m == Mode::Swing;
}

inline float roundWorldVy(float worldVy, bool flip) {
  double rel = static_cast<double>(worldVy) * (flip ? -1.0 : 1.0) * phys::TPS;
  const double sign = flip ? 1.0 : -1.0;
  double n = rel / 54.0 * sign;
  const double truncated = static_cast<int>(n);
  if (n != truncated) n = std::round((n - truncated) * 1000.0) / 1000.0 + truncated;
  rel = n * 54.0 * sign;
  return static_cast<float>(rel * (flip ? -1.0 : 1.0) / phys::TPS);
}

inline float cubePadUS(PadKind k, bool mini) {
  float v = 0.0f;
  switch (k) {
    case PadKind::Yellow: v = 864.0f; break;
    case PadKind::Pink: v = 561.6f; break;
    case PadKind::Red: v = 1080.0f; break;
    case PadKind::Blue: v = -345.6f; break;
  }
  return mini ? v * 0.8f : v;
}

inline float cubeOrbUS(OrbKind k, int tier, bool mini) {
  const int i = std::clamp(tier, 0, 3);
  static constexpr float yellow[4] = {573.48f,603.72f,616.68f,606.42f};
  static constexpr float blue[4]   = {-229.392f,-241.488f,-246.672f,-242.568f};
  static constexpr float pink[4]   = {412.884f,434.7f,443.988f,436.644f};
  static constexpr float red[4]    = {779.976f,821.448f,839.43f,825.174f};
  static constexpr float green[4]  = {562.032f,592.056f,605.07f,594.756f};
  float v = 0.0f;
  switch (k) {
    case OrbKind::Yellow: v=yellow[i]; break;
    case OrbKind::Blue: v=blue[i]; break;
    case OrbKind::Pink: v=pink[i]; break;
    case OrbKind::Red: v=red[i]; break;
    case OrbKind::Green: v=green[i]; break;
    case OrbKind::Black: v=-810.0f; break;
    case OrbKind::Dash: v=0.0f; break;
  }
  if (!mini) return v;
  if (k == OrbKind::Yellow) { static constexpr float a[4]={458.784f,482.976f,481.734f,485.136f}; return a[i]; }
  if (k == OrbKind::Blue)   { static constexpr float a[4]={-183.519f,-193.185f,-197.343f,-194.049f}; return a[i]; }
  if (k == OrbKind::Pink)   { static constexpr float a[4]={330.318f,347.76f,355.212f,349.272f}; return a[i]; }
  if (k == OrbKind::Red)    { static constexpr float a[4]={621.702f,654.858f,669.222f,657.828f}; return a[i]; }
  if (k == OrbKind::Green)  { static constexpr float a[4]={447.336f,471.312f,481.734f,485.136f}; return a[i]; }
  return v;
}

}  // namespace

void Sim::reset() {
  st_ = State();
  if (level_) {
    st_.y = level_->floorY + st_.halfH();
  }
  st_.speed = phys::SPEEDS[st_.tier];
}

bool Sim::step(bool hold) {
  if (!alive()) return false;
  lastContactUid_ = -1;
  deathUid_ = -1;

  const State prev = st_;
  const bool press = hold && !prev.holding;
  // Pathfinder/GD input buffering: only a button edge writes the buffer.
  // A held press stays buffered until an orb consumes it.
  if (hold != prev.holding) st_.buffer = hold;
  st_.holding = hold;
  st_.holdFrames = hold ? static_cast<uint16_t>(st_.holdFrames + 1) : 0;

  st_.x += st_.speed * phys::DT;
  st_.y += st_.vy;
  resolveWorld(prev);

  // Pathfinder/Geometry Dash has a deliberate edge-case when walking off an
  // elevated block: the player falls one native frame faster than the naive
  // integration predicts.  postCollision() adds one rounded previous-frame
  // acceleration displacement and, when velocity was zero, one extra rounded
  // acceleration contribution before the regular vehicle update.
  if (alive() && prev.mode == Mode::Cube && !prev.flip && prev.onGround &&
      !st_.onGround && (!hold || st_.buffer) && level_ &&
      (prev.y - prev.halfH()) > level_->floorY + 0.01f) {
    const int tier = std::clamp<int>(prev.tier, 0, 4);
    const float accelTick =
        -(phys::CUBE_ACCEL_U_S2[tier] / (phys::TPS * phys::TPS));
    const float roundedAccelTick = roundWorldVy(accelTick, prev.flip);
    st_.y += roundedAccelTick;
    if (std::fabs(st_.vy) < 1e-7f) st_.vy += roundedAccelTick;
  }

  if (alive()) applyMotion(press, hold);

  // Cosmetic rotation, exposed to the policy because it correlates with the
  // "can I still land flat" feeling human players rely on.
  if (st_.mode == Mode::Cube) {
    if (st_.onGround) {
      st_.rotation = std::round(st_.rotation / 90.0f) * 90.0f;
    } else {
      st_.rotation += phys::CUBE_ROT_PER_FRAME * st_.gdir();
    }
  }

  if (st_.y < phys::KILL_BELOW || st_.y > phys::CEILING_Y) st_.dead = true;
  if (level_ && st_.x >= level_->length) st_.won = true;
  if (++st_.frame >= phys::MAX_FRAMES) st_.dead = true;
  return alive();
}

// ------------------------------------------------------------ per-mode ------
void Sim::applyMotion(bool press, bool hold) {
  const float g = st_.gdir();
  auto clampFall = [&](float terminal) {
    if (st_.vy * g < -terminal) st_.vy = -terminal * g;
  };

  switch (st_.mode) {
    case Mode::Cube: {
      const int tier = std::clamp<int>(st_.tier, 0, 4);
      bool velocityOverride = false;
      if (st_.onGround) {
        if (hold) {
          const float rel = phys::CUBE_JUMP_U_S[tier] * (st_.mini ? 0.8f : 1.0f);
          st_.vy = (rel / phys::TPS) * g;
          st_.onGround = false;
          velocityOverride = !press;
        } else { st_.vy = 0.0f; velocityOverride = true; }
      }
      if (!velocityOverride) st_.vy -= (phys::CUBE_ACCEL_U_S2[tier] / (phys::TPS * phys::TPS)) * g;
      if (st_.vy * g < -phys::CUBE_TERMINAL) st_.vy = -phys::CUBE_TERMINAL * g;
      st_.vy = roundWorldVy(st_.vy, st_.flip);
      break;
    }
    case Mode::Ship: {
      st_.vy += (hold ? phys::SHIP_THRUST : -phys::SHIP_GRAVITY) * g;
      st_.vy = std::clamp(st_.vy, -phys::SHIP_MAX_VY, phys::SHIP_MAX_VY);
      break;
    }
    case Mode::Ball: {
      if (hold && st_.onGround) {
        st_.flip = !st_.flip;
        st_.onGround = false;
        st_.vy = 0;
      }
      st_.vy -= phys::BALL_GRAVITY * st_.gdir();
      clampFall(phys::BALL_TERMINAL);
      break;
    }
    case Mode::Ufo: {
      if (press) {
        st_.vy = phys::UFO_JUMP * g;
        st_.onGround = false;
      }
      st_.vy -= phys::UFO_GRAVITY * g;
      clampFall(phys::UFO_TERMINAL);
      break;
    }
    case Mode::Wave: {
      const float slope = st_.mini ? phys::WAVE_SLOPE_MINI : phys::WAVE_SLOPE;
      st_.vy = (hold ? 1.0f : -1.0f) * g * slope * st_.speed * phys::DT;
      break;
    }
    case Mode::Robot: {
      if (hold && st_.onGround && st_.jumpHold == 0) {
        st_.vy = phys::ROBOT_JUMP * g;
        st_.jumpHold = 1;
        st_.onGround = false;
      } else if (hold && st_.jumpHold > 0 &&
                 st_.jumpHold < phys::ROBOT_MAX_HOLD_FRAMES) {
        st_.vy += phys::ROBOT_HOLD_BOOST * g;
        st_.jumpHold++;
      } else if (!hold) {
        st_.jumpHold = 0;
      }
      st_.vy -= phys::ROBOT_GRAVITY * g;
      clampFall(phys::ROBOT_TERMINAL);
      break;
    }
    case Mode::Spider: {
      if (press) {
        // Instant teleport to the opposite surface, then invert gravity.
        const float probe = 30.0f;
        float target = st_.y;
        for (float d = probe; d < 12.0f * phys::BLOCK; d += 6.0f) {
          float ty = st_.y + d * (st_.flip ? -1.0f : 1.0f) * -1.0f;
          if (solidAt(st_.x, ty)) break;
          target = ty;
        }
        st_.y = target;
        st_.flip = !st_.flip;
        st_.vy = 0;
        st_.onGround = false;
      }
      st_.vy -= phys::SPIDER_GRAVITY * st_.gdir();
      clampFall(phys::SPIDER_TERMINAL);
      break;
    }
    case Mode::Swing: {
      if (press) st_.flip = !st_.flip;
      st_.vy -= phys::SWING_GRAVITY * st_.gdir();
      st_.vy = std::clamp(st_.vy, -phys::SWING_MAX_VY, phys::SWING_MAX_VY);
      break;
    }
    default:
      break;
  }
}

// --------------------------------------------------------- world contact ----
void Sim::resolveWorld(const State& prev) {
  if (!level_) return;
  const float prevY = prev.y;
  const float g = st_.gdir();
  const float hw = st_.halfW(), hh = st_.halfH();
  // Pathfinder uses the full unrotated player hitbox for ordinary hazards.
  // Solid side collisions still use the compact inner hitbox below.
  const float bhw = hw * phys::HITBOX_LETHAL_SCALE;
  const float bhh = hh * phys::HITBOX_LETHAL_SCALE;

  // Floor and ceiling are INDEPENDENT planes. They used to be an if/else on
  // gravity, so the ceiling only existed while flipped: a ship in normal
  // gravity had nothing above it and could fly over the entire level. The
  // level 1 macro cruised at 79.5 blocks while the tallest object sits at
  // 14.5, and still "verified" at 100%.
  // Which plane counts as ground depends on gravity; both still stop motion.
  if (st_.y - hh <= level_->floorY) {
    st_.y = level_->floorY + hh;
    st_.vy = 0;
    st_.jumpHold = 0;
    if (!st_.flip) st_.onGround = true;
    if (diesOnTouch(st_.mode)) st_.dead = true;
  }
  if (level_->roofY > 0 && st_.y + hh >= level_->roofY) {
    st_.y = level_->roofY - hh;
    st_.vy = 0;
    st_.jumpHold = 0;
    if (st_.flip) st_.onGround = true;
    if (diesOnTouch(st_.mode)) st_.dead = true;
  }

  // Flight ceiling, measured from the surface UNDER the player so elevated
  // corridors get an elevated ceiling -- this is what GD's camera does. A
  // static roof in the level file cannot express it: level 1's corridor
  // ceilings sit at 9.5-11.5 blocks in places and at 0 in others.
  {
    float ground = level_->floorY;
    int gn = 0;
    const int32_t* gidx = level_->bucketBegin(st_.x, &gn);
    if (gidx) {
      const std::vector<Object>& all = level_->objects();
      for (int i = 0; i < gn; ++i) {
        const Object& o = all[gidx[i]];
        if (o.kind != Kind::Solid) continue;
        if (std::fabs(o.x - st_.x) > o.hw + hw) continue;
        const float topY = o.y + o.hh;
        if (topY <= st_.y - hh + 2.0f && topY > ground) ground = topY;
      }
    }
    lastCeiling_ = ground + phys::FLIGHT_CEILING;
    if (isFlightMode(st_.mode) && st_.y + hh >= lastCeiling_) {
      st_.y = lastCeiling_ - hh;
      if (st_.vy > 0) st_.vy = 0;
      if (diesOnTouch(st_.mode)) st_.dead = true;
    }

    // No second flipped-gravity clamp is needed here: the dynamic
    // flight corridor above is defined in world space and already applies to
    // both gravity directions. A second remote clamp used to teleport inverted
    // ships below floorY when a nearby solid was less than one corridor-height
    // above them.
  }

  int n = 0;
  const int32_t* idx = level_->bucketBegin(st_.x, &n);
  if (!idx) return;
  const std::vector<Object>& objs = level_->objects();

  // Pass 1: portals and speed changes. They must be applied BEFORE collision
  // so a mode swap inside a tight corridor uses the new hitbox immediately.
  for (int i = 0; i < n; ++i) {
    const Object& o = objs[idx[i]];
    if (o.kind == Kind::Portal || o.kind == Kind::Speed) {
      const float mod90 = std::fmod(std::fabs(o.rotation), 90.0f);
      const bool cardinal = mod90 < 1e-4f || std::fabs(mod90 - 90.0f) < 1e-4f;
      const float playerRot = cardinal ? 0.0f : st_.rotation;
      const bool touching = std::fabs(o.rotation) < 1e-6f
          ? aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)
          : obbOverlap(st_.x, st_.y, hw, hh, playerRot,
                       o.x, o.y, o.hw, o.hh, o.rotation);
      if (touching) applyPortal(o);
    }
  }

  bool landed = false;
  for (int i = 0; i < n; ++i) {
    const Object& o = objs[idx[i]];
    if (o.kind != Kind::Solid) continue;
    if (slope::solidSlope(o)) {
      const bool touched = slope::resolveSolid(st_, o, prevY);
      if (touched) {
        lastContactUid_ = o.uid;
        if (st_.dead) { deathUid_ = o.uid; return; }
        if (st_.onGround) landed = true;
      }
      continue;
    }

    const float surfaceTop = o.y + o.hh * g;      // support face in gravity space
    const float surfaceBottom = o.y - o.hh * g;   // opposite face
    const float prevFeet = prevY - hh * g;
    const float prevHead = prevY + hh * g;
    const bool falling = st_.vy * g <= 0.0f;

    // Geometry Dash only calls Block::collide after Object::touching succeeds.
    // Its rectangle intersection is inclusive, so exact edge contact counts,
    // but a block must never pull the player across a free vertical gap.
    if (!aabbTouching(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)) continue;

    if (diesOnTouch(st_.mode)) {
      lastContactUid_ = o.uid;
      deathUid_ = o.uid;
      st_.dead = true;
      return;
    }
    if (falling && (prevFeet - surfaceTop) * g >= -1.0f) {
      st_.y = surfaceTop + hh * g;
      st_.vy = 0;
      st_.onGround = true;
      st_.jumpHold = 0;
      lastContactUid_ = o.uid;
      landed = true;
    } else if (!falling && (prevHead - surfaceBottom) * g <= 1.0f) {
      // Head bump on the underside of a block. In GD only the TOP face of a
      // solid is safe: banging your head while rising is a death, not a stop.
      // Treating it as a stop let the solver use ceilings as free brakes and
      // even jump into blocks above its head with no penalty.
      // Flying modes are the exception: a ship slides along a ceiling.
      if (!isFlightMode(st_.mode)) {
        lastContactUid_ = o.uid;
        deathUid_ = o.uid;
        st_.dead = true;
        return;
      }
      st_.y = surfaceBottom - hh * g;
      st_.vy = 0;
    } else if (aabbTouching(st_.x, st_.y, bhw, bhh, o.x, o.y, o.hw, o.hh)) {
      // Ran into the wall for real: that is a death in GD, not a slide.
      lastContactUid_ = o.uid;
      deathUid_ = o.uid;
      st_.dead = true;
      return;
    } else {
      // Corner forgiveness. GD kills on a solid's side only when the inner
      // hitbox is breached; clipping a corner by a couple of units is legal
      // and is precisely how staircases and tight gaps are climbed. Killing
      // on the outer box (my first version) made whole levels unsolvable and
      // the beam search proved it in seconds.
      const float pen = (surfaceTop - (st_.y - hh * g)) * g;
      // Pathfinder only allows a top-face snap while moving toward the
      // support surface (velocity <= 0 in gravity-relative coordinates).
      // Without this guard a rising cube gets magnetically pulled onto the
      // next platform a couple of ticks before reaching its apex.
      if (falling && pen > 0.0f && pen <= hh) {
        st_.y = surfaceTop + hh * g;
        if (st_.vy * g < 0) st_.vy = 0;
        st_.onGround = true;
        st_.jumpHold = 0;
        lastContactUid_ = o.uid;
        landed = true;
      }
    }
  }
  if (!landed && st_.onGround) {
    // Walked off a ledge: verify there is still ground under the feet.
    const float feet = st_.y - hh * g - 2.0f * g;
    const bool onPlane = !st_.flip ? (feet <= level_->floorY + 2.0f)
                                   : (level_->roofY > 0 &&
                                      feet >= level_->roofY - 2.0f);
    if (!onPlane && !solidAt(st_.x, feet)) st_.onGround = false;
  }

  for (int i = 0; i < n; ++i) {
    const Object& o = objs[idx[i]];
    if (o.kind == Kind::Pad) {
      if (o.uid != st_.lastPadUid &&
          aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh))
        applyPad(o);
    } else if (o.kind == Kind::Orb) {
      const bool touchingNow = aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh);
      const bool touchingPrev = aabb(prev.x, prev.y, prev.halfW(), prev.halfH(),
                                     o.x, o.y, o.hw, o.hh);
      // Orb::touching in Pathfinder is current OR previous frame.  The second
      // term reproduces release-coyote: a buffered press from the previous
      // frame may still fire while the button is now up.
      const bool canFire = st_.buffer || (prev.buffer && !st_.holding);
      if (canFire && o.uid != st_.lastOrbUid && (touchingNow || touchingPrev))
        applyOrb(o);
    }
  }
  // Hazards are checked after portals, solid contact correction and pads/orbs.
  // This avoids killing the pre-correction pose when GD would first snap the
  // player onto a valid surface or trigger an overlapping gameplay object.
  for (int i = 0; i < n; ++i) {
    const Object& o = objs[idx[i]];
    if (o.kind != Kind::Hazard) continue;
    const bool hit = slope::hazardSlope(o)
        ? slope::hazardHit(st_, o)
        : (o.sub == 1
            ? ellipseAabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)
            : aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh));
    if (hit) {
      lastContactUid_ = o.uid;
      deathUid_ = o.uid;
      st_.dead = true;
      return;
    }
  }

}

void Sim::applyPad(const Object& o) {
  lastContactUid_ = o.uid;
  st_.lastPadUid = o.uid;
  const PadKind k = static_cast<PadKind>(o.sub);
  if (st_.mode == Mode::Cube) {
    if (k == PadKind::Blue) st_.flip = !st_.flip;
    const float rel = cubePadUS(k, st_.mini);
    st_.vy = (rel / phys::TPS) * st_.gdir();
    st_.onGround = false;
    return;
  }
  switch (k) {
    case PadKind::Yellow: boost(phys::PAD_YELLOW); break;
    case PadKind::Pink: boost(phys::PAD_PINK); break;
    case PadKind::Red: boost(phys::PAD_RED); break;
    case PadKind::Blue: st_.flip=!st_.flip; boost(phys::PAD_BLUE,true); break;
  }
}

void Sim::applyOrb(const Object& o) {
  lastContactUid_ = o.uid;
  st_.lastOrbUid = o.uid;
  st_.buffer = false;
  const OrbKind k = static_cast<OrbKind>(o.sub);
  if (st_.mode == Mode::Cube) {
    if (k == OrbKind::Blue || k == OrbKind::Green) st_.flip = !st_.flip;
    const float rel = cubeOrbUS(k, st_.tier, st_.mini);
    st_.vy = (rel / phys::TPS) * st_.gdir();
    st_.onGround = false;
    return;
  }
  switch (k) {
    case OrbKind::Yellow: boost(phys::ORB_YELLOW); break;
    case OrbKind::Pink: boost(phys::ORB_PINK); break;
    case OrbKind::Red: boost(phys::ORB_RED); break;
    case OrbKind::Blue: st_.flip=!st_.flip; boost(phys::ORB_BLUE,true); break;
    case OrbKind::Green: st_.flip=!st_.flip; boost(phys::ORB_GREEN); break;
    case OrbKind::Black: boost(phys::ORB_BLACK,true); break;
    case OrbKind::Dash: boost(0.0f); break;
  }
}

void Sim::applyPortal(const Object& o) {
  lastContactUid_ = o.uid;
  if (o.kind == Kind::Speed) {
    st_.tier = static_cast<uint8_t>(std::min<int>(o.sub, 4));
    st_.speed = phys::SPEEDS[st_.tier];
    return;
  }
  // Portals are ABSOLUTE (blue = normal gravity, yellow = flipped, etc.), so
  // re-touching one is a no-op. That is what keeps State replay-safe.
  switch (static_cast<PortalKind>(o.sub)) {
    case PortalKind::ModeCube: st_.mode = Mode::Cube; break;
    case PortalKind::ModeShip: st_.mode = Mode::Ship; break;
    case PortalKind::ModeBall: st_.mode = Mode::Ball; break;
    case PortalKind::ModeUfo: st_.mode = Mode::Ufo; break;
    case PortalKind::ModeWave: st_.mode = Mode::Wave; break;
    case PortalKind::ModeRobot: st_.mode = Mode::Robot; break;
    case PortalKind::ModeSpider: st_.mode = Mode::Spider; break;
    case PortalKind::ModeSwing: st_.mode = Mode::Swing; break;
    case PortalKind::GravityNormal:
      if (st_.flip) {
        // Pathfinder GravityPortal: relative velocity becomes -v/2 while the
        // gravity basis flips. In gdlearn's world-space vy this preserves the
        // direction of travel and halves the magnitude.
        st_.vy *= 0.5f;
        st_.flip = false;
      }
      break;
    case PortalKind::GravityFlip:
      if (!st_.flip) {
        st_.vy *= 0.5f;
        st_.flip = true;
      }
      break;
    case PortalKind::SizeNormal: st_.mini = false; break;
    case PortalKind::SizeMini: st_.mini = true; break;
  }
}

// ------------------------------------------------------------- queries ------
bool Sim::solidAt(float x, float y) const {
  if (!level_) return false;
  int n = 0;
  const int32_t* idx = level_->bucketBegin(x, &n);
  if (!idx) return false;
  const std::vector<Object>& objs = level_->objects();
  for (int i = 0; i < n; ++i) {
    const Object& o = objs[idx[i]];
    if (o.kind != Kind::Solid) continue;
    if (slope::solidSlope(o)) { if (slope::pointInsideSolid(o, x, y)) return true; }
    else if (aabb(x, y, 0.5f, 0.5f, o.x, o.y, o.hw, o.hh)) return true;
  }
  return false;
}

bool Sim::hazardAt(float x, float y) const {
  if (!level_) return false;
  int n = 0;
  const int32_t* idx = level_->bucketBegin(x, &n);
  if (!idx) return false;
  const std::vector<Object>& objs = level_->objects();
  for (int i = 0; i < n; ++i) {
    const Object& o = objs[idx[i]];
    if (o.kind != Kind::Hazard) continue;
    if (slope::hazardSlope(o)) { if (slope::pointInsideHazard(o, x, y)) return true; }
    else if (o.sub == 1) {
      const float dx=(x-o.x)/std::max(0.001f,o.hw);
      const float dy=(y-o.y)/std::max(0.001f,o.hh);
      if (dx*dx+dy*dy <= 1.0f) return true;
    } else if (aabb(x,y,0.5f,0.5f,o.x,o.y,o.hw,o.hh)) return true;
  }
  return false;
}

const Object* Sim::interactiveAt(float x, float y) const {
  if (!level_) return nullptr;
  int n = 0;
  const int32_t* idx = level_->bucketBegin(x, &n);
  if (!idx) return nullptr;
  const std::vector<Object>& objs = level_->objects();
  for (int i = 0; i < n; ++i) {
    const Object& o = objs[idx[i]];
    const bool inter = o.kind == Kind::Orb || o.kind == Kind::Pad ||
                       o.kind == Kind::Portal || o.kind == Kind::Speed;
    if (inter && aabb(x, y, 0.5f, 0.5f, o.x, o.y, o.hw, o.hh)) return &o;
  }
  return nullptr;
}

}  // namespace gd

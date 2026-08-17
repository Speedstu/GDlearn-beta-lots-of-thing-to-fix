// SPDX-License-Identifier: MIT
#include "core/sim.hpp"

#include <algorithm>
#include <cmath>

namespace gd {
namespace {

inline bool aabb(float ax, float ay, float ahw, float ahh, float bx, float by,
                 float bhw, float bhh) {
  return std::fabs(ax - bx) < (ahw + bhw) && std::fabs(ay - by) < (ahh + bhh);
}

// Modes that die on any solid contact instead of landing on it.
inline bool diesOnTouch(Mode m) { return m == Mode::Wave; }

inline bool isFlightMode(Mode m) {
  return m == Mode::Ship || m == Mode::Ufo || m == Mode::Wave || m == Mode::Swing;
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

  const bool press = hold && !st_.holding;
  st_.holding = hold;
  st_.holdFrames = hold ? static_cast<uint16_t>(st_.holdFrames + 1) : 0;

  const float prevY = st_.y;
  applyMotion(press, hold);
  st_.x += st_.speed * phys::DT;
  resolveWorld(prevY);

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
      // Real GD buffers the button: holding makes the cube jump on every
      // ground contact, no release required. The old sim demanded a fresh
      // press, which made whole classes of jumps unreachable.
      if (hold && st_.onGround) {
        st_.vy = phys::CUBE_JUMP * g;
        st_.onGround = false;
      }
      st_.vy -= phys::CUBE_GRAVITY * g;
      clampFall(phys::CUBE_TERMINAL);
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
  st_.y += st_.vy;
}

// --------------------------------------------------------- world contact ----
void Sim::resolveWorld(float prevY) {
  if (!level_) return;
  const float g = st_.gdir();
  const float hw = st_.halfW(), hh = st_.halfH();
  const float lhw = hw * (1.0f - phys::HITBOX_LETHAL_SCALE);
  const float lhh = hh * (1.0f - phys::HITBOX_LETHAL_SCALE);
  // Solid sides use the compact inner player hitbox.  Hazards keep
  // their separately tuned lethal box above.
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
      if (aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)) applyPortal(o);
    }
  }

  // Pass 2: hazards (inner hitbox), then solids, then boosts.
  for (int i = 0; i < n; ++i) {
    const Object& o = objs[idx[i]];
    if (o.kind != Kind::Hazard) continue;
    if (aabb(st_.x, st_.y, lhw, lhh, o.x, o.y, o.hw, o.hh)) {
      lastContactUid_ = o.uid;
      deathUid_ = o.uid;
      st_.dead = true;
      return;
    }
  }

  bool landed = false;
  for (int i = 0; i < n; ++i) {
    const Object& o = objs[idx[i]];
    if (o.kind != Kind::Solid) continue;
    if (!aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh)) continue;

    if (diesOnTouch(st_.mode)) {
      lastContactUid_ = o.uid;
      deathUid_ = o.uid;
      st_.dead = true;
      return;
    }
    const float surfaceTop = o.y + o.hh * g;      // "floor" side in grav space
    const float surfaceBottom = o.y - o.hh * g;   // "ceiling" side
    const float prevFeet = prevY - hh * g;
    const float prevHead = prevY + hh * g;
    const bool falling = st_.vy * g <= 0.0f;

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
    } else if (aabb(st_.x, st_.y, bhw, bhh, o.x, o.y, o.hw, o.hh)) {
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
      if (pen > 0.0f && pen <= hh) {
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
      if (st_.holding && st_.holdFrames == 1 && o.uid != st_.lastOrbUid &&
          aabb(st_.x, st_.y, hw, hh, o.x, o.y, o.hw, o.hh))
        applyOrb(o);
    }
  }
}

void Sim::applyPad(const Object& o) {
  lastContactUid_ = o.uid;
  st_.lastPadUid = o.uid;
  switch (static_cast<PadKind>(o.sub)) {
    case PadKind::Yellow: boost(phys::PAD_YELLOW); break;
    case PadKind::Pink: boost(phys::PAD_PINK); break;
    case PadKind::Red: boost(phys::PAD_RED); break;
    case PadKind::Blue:
      st_.flip = !st_.flip;
      boost(phys::PAD_BLUE, /*alongGravity=*/true);
      break;
  }
}

void Sim::applyOrb(const Object& o) {
  lastContactUid_ = o.uid;
  st_.lastOrbUid = o.uid;
  switch (static_cast<OrbKind>(o.sub)) {
    case OrbKind::Yellow: boost(phys::ORB_YELLOW); break;
    case OrbKind::Pink: boost(phys::ORB_PINK); break;
    case OrbKind::Red: boost(phys::ORB_RED); break;
    case OrbKind::Blue:
      st_.flip = !st_.flip;
      boost(phys::ORB_BLUE, /*alongGravity=*/true);
      break;
    case OrbKind::Green:
      st_.flip = !st_.flip;
      boost(phys::ORB_GREEN);
      break;
    case OrbKind::Black: boost(phys::ORB_BLACK, /*alongGravity=*/true); break;
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
    case PortalKind::GravityNormal: st_.flip = false; break;
    case PortalKind::GravityFlip: st_.flip = true; break;
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
    if (o.kind == Kind::Solid && aabb(x, y, 0.5f, 0.5f, o.x, o.y, o.hw, o.hh))
      return true;
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
    if (o.kind == Kind::Hazard && aabb(x, y, 0.5f, 0.5f, o.x, o.y, o.hw, o.hh))
      return true;
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

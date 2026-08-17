// SPDX-License-Identifier: MIT
// Deterministic Geometry Dash physics simulator.
//
// Design rules that make this usable for BOTH reinforcement learning and
// exact tree search (the old codebase could do neither cleanly):
//   1. `State` is a trivially-copyable POD. Snapshot / restore is a memcpy,
//      so beam search can fork thousands of timelines per frame.
//   2. Zero allocation and zero mutation of the Level inside step().
//   3. Every trigger is idempotent OR guarded by a uid stored in the State,
//      so replaying the same input sequence always gives the same result.
#pragma once

#include <cstdint>
#include <cstring>

#include "core/level.hpp"
#include "core/physics.hpp"

namespace gd {

struct State {
  float x = 0, y = 0;
  float vy = 0;
  float rotation = 0;
  float speed = phys::SPEEDS[1];
  int32_t frame = 0;
  int32_t lastOrbUid = -1;   // orb consumed most recently (once per contact)
  int32_t lastPadUid = -1;
  uint16_t holdFrames = 0;   // frames the button has been held
  uint16_t jumpHold = 0;     // robot variable-jump window counter
  Mode mode = Mode::Cube;
  uint8_t tier = 1;          // speed tier index
  bool flip = false;         // gravity points up
  bool mini = false;
  bool onGround = true;
  bool holding = false;
  bool dead = false;
  bool won = false;

  inline float gdir() const { return flip ? -1.0f : 1.0f; }
  inline float halfW() const { return phys::HITBOX_CUBE * (mini ? phys::MINI_SCALE : 1.0f); }
  inline float halfH() const { return phys::HITBOX_CUBE * (mini ? phys::MINI_SCALE : 1.0f); }
};
static_assert(sizeof(State) <= 64, "keep State cache-friendly");

class Sim {
 public:
  explicit Sim(const Level* level = nullptr) { setLevel(level); }

  void setLevel(const Level* level) {
    level_ = level;
    reset();
  }
  const Level* level() const { return level_; }

  void reset();
  void restore(const State& s) { st_ = s; }
  const State& state() const { return st_; }
  // Local flight ceiling at the player position, recomputed every frame.
  float ceiling() const { return lastCeiling_; }
  State& mutableState() { return st_; }

  // Advance exactly one 60Hz gameplay frame. Returns false once the attempt
  // is over (death, finish, or timeout).
  bool step(bool hold);

  inline bool alive() const { return !st_.dead && !st_.won; }
  inline float progress() const {
    if (!level_ || level_->length <= 0) return 0;
    float p = st_.x / level_->length;
    return p < 0 ? 0 : (p > 1 ? 1 : p);
  }

  // Cheap world queries, used by the observation encoder.
  bool solidAt(float x, float y) const;
  bool hazardAt(float x, float y) const;
  const Object* interactiveAt(float x, float y) const;

 private:
  void applyMotion(bool press, bool hold);
  void resolveWorld(float prevY);
  void applyPad(const Object& o);
  void applyOrb(const Object& o);
  void applyPortal(const Object& o);
  inline void boost(float v, bool alongGravity = false) {
    st_.vy = alongGravity ? -v * st_.gdir() : v * st_.gdir();
    st_.onGround = false;
  }

  const Level* level_ = nullptr;
  State st_{};
  float lastCeiling_ = 0;
};

}  // namespace gd

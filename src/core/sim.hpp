// SPDX-License-Identifier: MIT
// Deterministic Geometry Dash physics simulator.
//
// Design rules that make this usable for BOTH reinforcement learning and
// exact tree search:
//   1. `State` is a trivially-copyable POD. Snapshot / restore is a memcpy,
//      so beam search can fork thousands of timelines per native tick.
//   2. Zero allocation and zero mutation of the Level inside step().
//   3. Every trigger is idempotent OR guarded by a uid stored in the State,
//      so replaying the same 240-TPS input sequence is deterministic.
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
  int32_t frame = 0;            // native physics tick (legacy field name)
  int32_t lastOrbUid = -1;
  int32_t lastPadUid = -1;
  uint16_t holdFrames = 0;      // native ticks button has been held
  uint16_t jumpHold = 0;
  Mode mode = Mode::Cube;
  uint8_t tier = 1;
  bool flip = false;
  bool mini = false;
  bool onGround = true;
  bool holding = false;
  bool buffer = false;
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
  float ceiling() const { return lastCeiling_; }
  int lastContactUid() const { return lastContactUid_; }
  int deathUid() const { return deathUid_; }
  State& mutableState() { return st_; }

  // Advance exactly one native Geometry Dash physics tick (240 TPS).
  bool step(bool hold);

  inline bool alive() const { return !st_.dead && !st_.won; }
  inline float progress() const {
    if (!level_ || level_->length <= 0) return 0;
    float p = st_.x / level_->length;
    return p < 0 ? 0 : (p > 1 ? 1 : p);
  }

  bool solidAt(float x, float y) const;
  bool hazardAt(float x, float y) const;
  const Object* interactiveAt(float x, float y) const;

 private:
  void applyMotion(bool press, bool hold);
  void resolveWorld(const State& prev);
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
  int lastContactUid_ = -1;
  int deathUid_ = -1;
};

}  // namespace gd

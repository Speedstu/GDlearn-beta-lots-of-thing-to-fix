// SPDX-License-Identifier: MIT
// Level representation + O(1) spatial lookup.
//
// A Level is IMMUTABLE once built. The simulator never mutates it, which is
// what lets us share one Level across N worker threads with zero locking.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gd {

enum class Kind : uint8_t {
  Solid,      // resolves collision, lethal from the side
  Hazard,     // instant death on inner-hitbox overlap
  Pad,        // auto boost on touch
  Orb,        // boost when a fresh press happens while overlapping
  Portal,     // gamemode / gravity / size / dual
  Speed,      // speed tier change
  Deco,       // ignored by gameplay (never stored)
};

enum class Mode : uint8_t {
  Cube = 0, Ship, Ball, Ufo, Wave, Robot, Spider, Swing, COUNT
};

// Sub-kinds, kept flat so an Object stays a small POD.
enum class PadKind : uint8_t { Yellow, Pink, Red, Blue };
enum class OrbKind : uint8_t { Yellow, Pink, Red, Blue, Green, Black, Dash };
enum class PortalKind : uint8_t {
  ModeCube, ModeShip, ModeBall, ModeUfo, ModeWave, ModeRobot, ModeSpider,
  ModeSwing, GravityNormal, GravityFlip, SizeNormal, SizeMini,
};

struct Object {
  float x = 0, y = 0;         // centre, units
  float hw = 15, hh = 15;     // local half extents after scale
  float rotation = 0;         // degrees; 0 for legacy .gdl objects
  Kind kind = Kind::Solid;
  uint8_t sub = 0;            // PadKind / OrbKind / PortalKind / speed tier
  int16_t id = 0;             // original GD object id (debug / round-trip)
  int32_t uid = 0;            // stable index, used for once-per-attempt logic
};

// Uniform grid over X only: GD levels are long corridors, so a 1-D bucket
// list is both simpler and faster than a quadtree.
class Level {
 public:
  static constexpr float kBucket = 60.0f;  // 2 blocks

  std::string name;
  float length = 0;   // x of the finish line, units
  float floorY = 0;   // y of the floor surface
  float roofY = 0;     // 0 == no ceiling

  void add(Object o);
  void finalize();     // computes length, builds buckets. Call once.

  const std::vector<Object>& objects() const { return objs_; }
  int objectCount() const { return static_cast<int>(objs_.size()); }

  // Indices of every object whose bucket can touch [x-pad, x+pad].
  // Returned as a contiguous span into a precomputed CSR array.
  inline const int32_t* bucketBegin(float x, int* count) const {
    int b = bucketOf(x);
    if (b < 0 || b >= static_cast<int>(rowStart_.size()) - 1) {
      *count = 0;
      return nullptr;
    }
    *count = rowStart_[b + 1] - rowStart_[b];
    return cells_.data() + rowStart_[b];
  }

  inline int bucketOf(float x) const {
    int b = static_cast<int>((x - minX_) / kBucket);
    if (b < 0) b = 0;
    int last = static_cast<int>(rowStart_.size()) - 2;
    if (b > last) b = last;
    return b;
  }

  // ---- IO -----------------------------------------------------------------
  // .gdl is our own line-oriented text format, produced by
  // tools/gmd_to_gdl.py. Keeping the messy GD base64/gzip/legacy-property
  // decoding in Python keeps this core clean, testable and dependency-free.
  static Level loadGdl(const std::string& path);
  void saveGdl(const std::string& path) const;

 private:
  std::vector<Object> objs_;
  std::vector<int32_t> cells_;     // CSR values: object indices
  std::vector<int32_t> rowStart_;  // CSR row offsets, size = nbuckets + 1
  float minX_ = 0;
};

// Deterministic procedural levels used for smoke tests and curriculum warmup.
Level makeTrainingLevel(int difficulty, uint64_t seed);
Level makeFlatLevel(float lengthBlocks);

}  // namespace gd

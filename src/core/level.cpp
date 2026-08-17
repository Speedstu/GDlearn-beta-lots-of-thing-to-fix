// SPDX-License-Identifier: MIT
#include "core/level.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "core/physics.hpp"
#include "core/rng.hpp"

namespace gd {
namespace {

const char* kKindNames[] = {"solid", "hazard", "pad", "orb",
                            "portal", "speed", "deco"};

Kind kindFromName(const std::string& s) {
  for (int i = 0; i < 7; ++i)
    if (s == kKindNames[i]) return static_cast<Kind>(i);
  throw std::runtime_error("unknown object kind: " + s);
}

}  // namespace

void Level::add(Object o) {
  o.uid = static_cast<int32_t>(objs_.size());
  objs_.push_back(o);
}

void Level::finalize() {
  if (objs_.empty()) {
    rowStart_ = {0, 0};
    return;
  }
  // Gameplay length = a bit past the last thing that can kill or carry you.
  float maxX = 0, minX = 0;
  bool first = true;
  for (const Object& o : objs_) {
    float l = o.x - o.hw, r = o.x + o.hw;
    if (first) { minX = l; maxX = r; first = false; }
    minX = std::min(minX, l);
    maxX = std::max(maxX, r);
  }
  minX_ = minX - phys::BLOCK;
  if (length <= 0) length = maxX + 5.0f * phys::BLOCK;

  const int nb = static_cast<int>((maxX - minX_) / kBucket) + 2;
  std::vector<int32_t> counts(nb, 0);
  auto span = [&](const Object& o, int* lo, int* hi) {
    // Pad by one bucket on both sides so a query only ever needs ONE bucket.
    *lo = static_cast<int>((o.x - o.hw - minX_) / kBucket) - 1;
    *hi = static_cast<int>((o.x + o.hw - minX_) / kBucket) + 1;
    *lo = std::max(*lo, 0);
    *hi = std::min(*hi, nb - 1);
  };
  for (const Object& o : objs_) {
    int lo, hi;
    span(o, &lo, &hi);
    for (int b = lo; b <= hi; ++b) counts[b]++;
  }
  rowStart_.assign(nb + 1, 0);
  for (int b = 0; b < nb; ++b) rowStart_[b + 1] = rowStart_[b] + counts[b];
  cells_.assign(rowStart_.back(), 0);
  std::vector<int32_t> cursor(rowStart_.begin(), rowStart_.end() - 1);
  for (const Object& o : objs_) {
    int lo, hi;
    span(o, &lo, &hi);
    for (int b = lo; b <= hi; ++b) cells_[cursor[b]++] = o.uid;
  }
}

// ---------------------------------------------------------------------- IO --

Level Level::loadGdl(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open level: " + path);
  Level lv;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;
    if (tag == "name") {
      std::getline(ss, lv.name);
      if (!lv.name.empty() && lv.name[0] == ' ') lv.name.erase(0, 1);
    } else if (tag == "floor") {
      ss >> lv.floorY;
    } else if (tag == "roof") {
      ss >> lv.roofY;
    } else if (tag == "length") {
      ss >> lv.length;
    } else if (tag == "o") {
      std::string kind;
      Object o;
      int sub = 0, id = 0;
      ss >> kind >> sub >> o.x >> o.y >> o.hw >> o.hh >> id;
      o.kind = kindFromName(kind);
      o.sub = static_cast<uint8_t>(sub);
      o.id = static_cast<int16_t>(id);
      if (o.kind != Kind::Deco) lv.add(o);
    }
  }
  lv.finalize();
  return lv;
}

void Level::saveGdl(const std::string& path) const {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write level: " + path);
  out << "# gdlearn level v1\n";
  out << "name " << name << "\n";
  out << "floor " << floorY << "\nroof " << roofY << "\nlength " << length
      << "\n";
  for (const Object& o : objs_) {
    out << "o " << kKindNames[static_cast<int>(o.kind)] << " "
        << static_cast<int>(o.sub) << " " << o.x << " " << o.y << " " << o.hw
        << " " << o.hh << " " << o.id << "\n";
  }
}

// ------------------------------------------------------- procedural levels --
namespace {

Object block(float bx, float by, float wBlocks = 1, float hBlocks = 1) {
  Object o;
  o.kind = Kind::Solid;
  o.hw = wBlocks * phys::BLOCK * 0.5f;
  o.hh = hBlocks * phys::BLOCK * 0.5f;
  o.x = bx * phys::BLOCK + o.hw;
  o.y = by * phys::BLOCK + o.hh;
  o.id = 1;
  return o;
}

Object spike(float bx, float by) {
  Object o;
  o.kind = Kind::Hazard;
  // GD spike hitbox is much smaller than its sprite: ~8x16 units.
  o.hw = 4.0f;
  o.hh = 8.0f;
  o.x = bx * phys::BLOCK + phys::BLOCK * 0.5f;
  o.y = by * phys::BLOCK + 8.0f;
  o.id = 8;
  return o;
}

Object pad(float bx, float by, PadKind k) {
  Object o;
  o.kind = Kind::Pad;
  o.sub = static_cast<uint8_t>(k);
  o.hw = 15.0f;
  o.hh = 5.0f;
  o.x = bx * phys::BLOCK + 15.0f;
  o.y = by * phys::BLOCK + 5.0f;
  return o;
}

Object orb(float bx, float by, OrbKind k) {
  Object o;
  o.kind = Kind::Orb;
  o.sub = static_cast<uint8_t>(k);
  o.hw = 18.0f;
  o.hh = 18.0f;
  o.x = bx * phys::BLOCK + 15.0f;
  o.y = by * phys::BLOCK + 15.0f;
  return o;
}

Object portal(float bx, float by, PortalKind k) {
  Object o;
  o.kind = Kind::Portal;
  o.sub = static_cast<uint8_t>(k);
  o.hw = 12.0f;
  o.hh = 45.0f;
  o.x = bx * phys::BLOCK + 15.0f;
  o.y = by * phys::BLOCK + 45.0f;
  return o;
}

Object speedPortal(float bx, float by, int tier) {
  Object o;
  o.kind = Kind::Speed;
  o.sub = static_cast<uint8_t>(tier);
  o.hw = 10.0f;
  o.hh = 20.0f;
  o.x = bx * phys::BLOCK + 15.0f;
  o.y = by * phys::BLOCK + 20.0f;
  return o;
}

}  // namespace

Level makeFlatLevel(float lengthBlocks) {
  Level lv;
  lv.name = "flat";
  lv.length = lengthBlocks * phys::BLOCK;
  lv.finalize();
  return lv;
}

// difficulty 0..9. Each tier adds a new mechanic, which gives the RL agent a
// natural curriculum without any hand-written level files.
Level makeTrainingLevel(int difficulty, uint64_t seed) {
  Rng rng(seed * 6364136223846793005ull + difficulty + 1);
  Level lv;
  lv.name = "proc_d" + std::to_string(difficulty) + "_s" + std::to_string(seed);

  const int segments = 8 + difficulty * 3;
  float bx = 6;  // leave a runway so the agent always starts safe

  for (int s = 0; s < segments; ++s) {
    const int roll = rng.below(difficulty >= 6 ? 7 : (difficulty >= 3 ? 5 : 3));
    switch (roll) {
      case 0: {  // single or double spike on the floor
        lv.add(spike(bx, 0));
        if (difficulty >= 2 && rng.chance(0.35f)) lv.add(spike(bx + 1, 0));
        bx += 4 + rng.below(3);
        break;
      }
      case 1: {  // platform to hop onto
        float h = 1 + static_cast<float>(rng.below(1 + difficulty / 3));
        lv.add(block(bx, 0, 3, h));
        if (difficulty >= 4) lv.add(spike(bx + 3.5f, 0));
        bx += 6 + rng.below(3);
        break;
      }
      case 2: {  // jump pad chain
        lv.add(pad(bx, 0, rng.chance(0.5f) ? PadKind::Yellow : PadKind::Pink));
        lv.add(block(bx + 3, 0, 2, 3));
        bx += 7 + rng.below(2);
        break;
      }
      case 3: {  // ring/orb over a pit
        lv.add(block(bx, 0, 2, 1));
        lv.add(spike(bx + 2, 0));
        lv.add(spike(bx + 3, 0));
        lv.add(orb(bx + 2.5f, 2, OrbKind::Yellow));
        bx += 7 + rng.below(2);
        break;
      }
      case 4: {  // stair climb
        for (int i = 0; i < 3; ++i)
          lv.add(block(bx + i * 2, static_cast<float>(i), 2, 1));
        bx += 9;
        break;
      }
      case 5: {  // ship corridor
        lv.add(portal(bx, 0, PortalKind::ModeShip));
        const int len = 10 + rng.below(8);
        for (int i = 0; i < len; ++i) {
          float gapCenter = 4.0f + 2.0f * std::sin(0.6f * static_cast<float>(i));
          lv.add(block(bx + 2 + i, gapCenter + 2.0f, 1, 6));
          lv.add(block(bx + 2 + i, gapCenter - 8.0f, 1, 6));
        }
        lv.add(portal(bx + 3 + len, 0, PortalKind::ModeCube));
        bx += len + 8;
        break;
      }
      default: {  // speed change + tight spike timing
        lv.add(speedPortal(bx, 0, 2 + rng.below(2)));
        lv.add(spike(bx + 3, 0));
        lv.add(block(bx + 5, 0, 2, 2));
        lv.add(spike(bx + 7.5f, 0));
        bx += 11;
        break;
      }
    }
  }
  lv.length = (bx + 6) * phys::BLOCK;
  lv.finalize();
  return lv;
}

}  // namespace gd

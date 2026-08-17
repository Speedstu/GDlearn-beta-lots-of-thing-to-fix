// SPDX-License-Identifier: MIT
#include "env/obs.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace gd {

int obsDim() { return ObsSpec::kDim; }

void encodeObs(const Sim& sim, float* out) {
  std::memset(out, 0, sizeof(float) * ObsSpec::kDim);
  const State& s = sim.state();
  const Level* lv = sim.level();
  const float g = s.gdir();
  const float cell = phys::BLOCK;

  for (int c = 0; c < ObsSpec::kCols; ++c) {
    const float px = s.x + (static_cast<float>(c) + 0.5f) * cell;
    for (int r = 0; r < ObsSpec::kRows; ++r) {
      const float dy = (static_cast<float>(r) - ObsSpec::kRows * 0.5f + 0.5f) * cell;
      const float py = s.y + dy * g;
      const int base = (c * ObsSpec::kRows + r) * ObsSpec::kChannels;
      bool solid = sim.solidAt(px, py);
      if (!solid && lv) {
        if (py <= lv->floorY) solid = true;
        if (lv->roofY > 0 && py >= lv->roofY) solid = true;
      }
      if (solid) out[base] = 1.0f;
      if (sim.hazardAt(px, py)) out[base + 1] = 1.0f;
      if (const Object* o = sim.interactiveAt(px, py)) {
        float v = 0.25f;
        if (o->kind == Kind::Orb) v = 0.5f + 0.05f * static_cast<float>(o->sub);
        else if (o->kind == Kind::Pad) v = -0.5f - 0.05f * static_cast<float>(o->sub);
        else if (o->kind == Kind::Portal) v = 1.0f;
        else if (o->kind == Kind::Speed) v = -1.0f;
        out[base + 2] = v;
      }
    }
  }

  float* sc = out + ObsSpec::kGrid;
  int i = 0;
  sc[i++] = std::tanh((s.y - (lv ? lv->floorY : 0.0f)) / (10.0f * cell));
  sc[i++] = std::tanh(s.vy / 12.0f);
  sc[i++] = s.vy * g > 0 ? 1.0f : 0.0f;
  sc[i++] = s.speed / phys::SPEEDS[4];
  sc[i++] = s.onGround ? 1.0f : 0.0f;
  sc[i++] = s.flip ? 1.0f : 0.0f;
  sc[i++] = s.mini ? 1.0f : 0.0f;
  sc[i++] = s.holding ? 1.0f : 0.0f;
  sc[i++] = std::tanh(static_cast<float>(s.holdFrames) / 20.0f);
  sc[i++] = std::tanh(static_cast<float>(s.jumpHold) / 15.0f);
  sc[i++] = std::sin(s.rotation * 3.14159265f / 180.0f);
  sc[i++] = std::cos(s.rotation * 3.14159265f / 180.0f);
  for (int m = 0; m < static_cast<int>(Mode::COUNT); ++m)
    sc[i++] = static_cast<int>(s.mode) == m ? 1.0f : 0.0f;
  for (int t = 0; t < 5; ++t) sc[i++] = s.tier == t ? 1.0f : 0.0f;
  {
    float fx = s.x / cell; fx -= std::floor(fx);
    float fy = (s.y - (lv ? lv->floorY : 0.0f)) / cell; fy -= std::floor(fy);
    sc[i++] = fx;
    sc[i++] = std::sin(6.2831853f * fx);
    sc[i++] = std::cos(6.2831853f * fx);
    sc[i++] = fy;
  }
  while (i < ObsSpec::kScalars) sc[i++] = 0.0f;

  if (!lv) return;
  std::array<const Object*, ObsSpec::kObjectTokens> nearest{};
  std::array<float, ObsSpec::kObjectTokens> edgeDist{};
  int count = 0;
  const float maxX = s.x + static_cast<float>(ObsSpec::kCols) * cell;
  const float minX = s.x - s.halfW();

  auto consider = [&](const Object& o) {
    if (o.x + o.hw < minX || o.x - o.hw > maxX) return;
    for (int k = 0; k < count; ++k)
      if (nearest[k] && nearest[k]->uid == o.uid) return;
    const float d = o.x - o.hw - s.x;
    int pos = count;
    if (count < ObsSpec::kObjectTokens) ++count;
    else {
      if (d >= edgeDist[ObsSpec::kObjectTokens - 1]) return;
      pos = ObsSpec::kObjectTokens - 1;
    }
    while (pos > 0 && d < edgeDist[pos - 1]) {
      edgeDist[pos] = edgeDist[pos - 1];
      nearest[pos] = nearest[pos - 1];
      --pos;
    }
    edgeDist[pos] = d;
    nearest[pos] = &o;
  };

  for (float qx = s.x; qx <= maxX + 0.5f * Level::kBucket; qx += Level::kBucket) {
    int n = 0;
    const int32_t* ids = lv->bucketBegin(qx, &n);
    if (!ids) continue;
    const auto& objects = lv->objects();
    for (int k = 0; k < n; ++k) consider(objects[ids[k]]);
  }

  float* tok = out + ObsSpec::kGrid + ObsSpec::kScalars;
  const float look = static_cast<float>(ObsSpec::kCols) * cell;
  for (int k = 0; k < count; ++k) {
    const Object& o = *nearest[k];
    float* t = tok + k * ObsSpec::kTokenFeatures;
    t[0] = std::clamp((o.x - s.x) / look, -0.25f, 1.25f);
    t[1] = std::tanh(((o.y - s.y) * g) / (6.0f * cell));
    t[2] = std::clamp((o.x - o.hw - s.x) / look, -0.25f, 1.25f);
    t[3] = std::clamp(o.hw / (3.0f * cell), 0.0f, 1.0f);
    t[4] = std::clamp(o.hh / (3.0f * cell), 0.0f, 1.0f);
    const int kind = static_cast<int>(o.kind);
    for (int j = 0; j < 6; ++j) t[5 + j] = kind == j ? 1.0f : 0.0f;
    t[11] = std::clamp(static_cast<float>(o.sub) / 12.0f, 0.0f, 1.0f);
  }
}

}  // namespace gd

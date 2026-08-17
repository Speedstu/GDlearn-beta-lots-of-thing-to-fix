// SPDX-License-Identifier: MIT
#include "env/obs.hpp"

#include <algorithm>
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

  // ---------------------------------------------------------------- grid ----
  // Column c covers x in [x + c*cell, x + (c+1)*cell).
  // Row r covers gravity-relative y offset (r - kRows/2) blocks.
  for (int c = 0; c < ObsSpec::kCols; ++c) {
    const float px = s.x + (static_cast<float>(c) + 0.5f) * cell;
    for (int r = 0; r < ObsSpec::kRows; ++r) {
      const float dy = (static_cast<float>(r) - ObsSpec::kRows * 0.5f + 0.5f) * cell;
      const float py = s.y + dy * g;  // gravity-relative: "up" is always +row
      const int base = (c * ObsSpec::kRows + r) * ObsSpec::kChannels;

      bool solid = sim.solidAt(px, py);
      if (!solid && lv) {
        // Treat the floor/ceiling planes as solid so the agent sees them.
        if (py <= lv->floorY) solid = true;
        if (lv->roofY > 0 && py >= lv->roofY) solid = true;
      }
      if (solid) out[base + 0] = 1.0f;
      if (sim.hazardAt(px, py)) out[base + 1] = 1.0f;
      if (const Object* o = sim.interactiveAt(px, py)) {
        // Encode which interactive it is as a signed magnitude: cheap, and
        // keeps the channel count at 3.
        float v = 0.25f;
        if (o->kind == Kind::Orb) v = 0.5f + 0.05f * static_cast<float>(o->sub);
        else if (o->kind == Kind::Pad) v = -0.5f - 0.05f * static_cast<float>(o->sub);
        else if (o->kind == Kind::Portal) v = 1.0f;
        else if (o->kind == Kind::Speed) v = -1.0f;
        out[base + 2] = v;
      }
    }
  }

  // ------------------------------------------------------------- scalars ----
  float* sc = out + ObsSpec::kGrid;
  int i = 0;
  sc[i++] = std::tanh((s.y - (lv ? lv->floorY : 0.0f)) / (10.0f * cell));
  sc[i++] = std::tanh(s.vy / 12.0f);
  sc[i++] = s.vy * g > 0 ? 1.0f : 0.0f;            // moving "up"
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
    sc[i++] = (static_cast<int>(s.mode) == m) ? 1.0f : 0.0f;
  for (int t = 0; t < 5; ++t) sc[i++] = (s.tier == t) ? 1.0f : 0.0f;
  // Sub-block phase. The grid above is quantised to whole blocks, so without
  // this the network literally cannot see WHERE inside a block it is, and
  // frame-perfect timing becomes unlearnable. sin/cos make the wrap-around
  // continuous instead of jumping from 1 back to 0.
  {
    float fx = s.x / cell;
    fx -= std::floor(fx);
    float fy = (s.y - (lv ? lv->floorY : 0.0f)) / cell;
    fy -= std::floor(fy);
    sc[i++] = fx;
    sc[i++] = std::sin(6.2831853f * fx);
    sc[i++] = std::cos(6.2831853f * fx);
    sc[i++] = fy;
  }
  // Time-in-attempt is deliberately NOT included: it would let the policy
  // memorise a level instead of reacting to geometry.
  while (i < ObsSpec::kScalars) sc[i++] = 0.0f;
}

}  // namespace gd

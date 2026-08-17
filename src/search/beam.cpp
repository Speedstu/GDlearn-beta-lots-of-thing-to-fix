// SPDX-License-Identifier: MIT
#include "search/beam.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace gd {
namespace {

// Two states are "the same" for search purposes if the player is in the same
// place with the same momentum and the same form. Quantisation is what keeps
// the beam from filling up with 800 copies of one situation.
// Quantisation must stay finer than the margins that decide life and death.
// GD jump windows are routinely won or lost by ~2 units of height, so a
// 2-unit bucket (the first thing I tried) silently deletes the only surviving
// branch. 0.5 units of height and 0.25 of velocity keep every real option.
inline uint64_t stateKey(const State& s) {
  const int64_t qx = static_cast<int64_t>(s.x * 0.5f);
  const int64_t qy = static_cast<int64_t>(std::lround(s.y * 2.0f));
  const int64_t qv = static_cast<int64_t>(std::lround(s.vy * 4.0f));
  uint64_t h = 1469598103934665603ull;
  auto mix = [&h](uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
  };
  mix(static_cast<uint64_t>(qx));
  mix(static_cast<uint64_t>(qy + 100000));
  mix(static_cast<uint64_t>(qv + 1000));
  mix(static_cast<uint64_t>(s.mode));
  mix(static_cast<uint64_t>(s.tier));
  mix((s.flip ? 1u : 0u) | (s.mini ? 2u : 0u) | (s.onGround ? 4u : 0u) |
      (s.holding ? 8u : 0u));
  mix(static_cast<uint64_t>(s.jumpHold));
  return h;
}

struct Cand {
  State st;
  float score;
  int32_t parent;
  uint8_t action;
};

}  // namespace

SolveResult beamSolve(const Level& level, const SolveOptions& opts) {
  SolveResult res;
  Sim sim(&level);
  State root = sim.state();
  if (opts.hasStart) root = opts.start;

  std::vector<State> cur{root};
  // trail[f][i] = (parent slot in frame f-1, action taken to reach slot i)
  std::vector<std::vector<std::pair<int32_t, uint8_t>>> trail;
  trail.reserve(1024);

  float record = 0;
  int recordFrame = 0;
  int bestFrame = 0, bestSlot = 0;
  std::vector<Cand> cands;
  std::unordered_map<uint64_t, int> seen;

  for (int frame = 0; frame < opts.maxFrames; ++frame) {
    cands.clear();
    cands.reserve(cur.size() * 2);
    seen.clear();
    seen.reserve(cur.size() * 4);

    for (int32_t i = 0; i < static_cast<int32_t>(cur.size()); ++i) {
      for (uint8_t a = 0; a < 2; ++a) {
        sim.restore(cur[i]);
        sim.step(a != 0);
        res.expanded++;
        const State& ns = sim.state();
        if (ns.dead) continue;

        // x is a deterministic function of the frame, so it cannot rank
        // siblings. Break ties on clearance above the surface (in gravity
        // space) and on remaining speed: both correlate with "still has
        // options next frame".
        float score = ns.x + 0.02f * (ns.y * ns.gdir()) + (ns.onGround ? 0.5f : 0.0f);
        if (opts.prior && opts.priorWeight > 0) {
          const float p = std::clamp(opts.prior(cur[i]), 1e-4f, 1.0f - 1e-4f);
          score += opts.priorWeight * std::log(a ? p : 1.0f - p);
        }
        if (ns.won) {
          // Finish line: reconstruct immediately.
          trail.push_back({{i, a}});
          res.solved = true;
          res.progress = 1.0f;
          res.finalState = ns;
          bestFrame = static_cast<int>(trail.size()) - 1;
          bestSlot = 0;
          goto reconstruct;
        }
        const uint64_t key = stateKey(ns);
        auto it = seen.find(key);
        if (it == seen.end()) {
          seen.emplace(key, static_cast<int>(cands.size()));
          cands.push_back({ns, score, i, a});
        } else if (cands[it->second].score < score) {
          cands[it->second] = {ns, score, i, a};
        }
      }
    }

    if (cands.empty()) break;  // every branch dies: this is the wall

    const int keep = std::min<int>(opts.beamWidth, static_cast<int>(cands.size()));
    std::partial_sort(cands.begin(), cands.begin() + keep, cands.end(),
                      [](const Cand& a, const Cand& b) {
                        return a.score > b.score;
                      });
    cands.resize(keep);

    cur.clear();
    cur.reserve(keep);
    std::vector<std::pair<int32_t, uint8_t>> row;
    row.reserve(keep);
    for (const Cand& c : cands) {
      cur.push_back(c.st);
      row.emplace_back(c.parent, c.action);
    }
    trail.push_back(std::move(row));

    const float p = cands[0].st.x / level.length;
    if (p > record + 1e-5f) {
      record = p;
      recordFrame = frame;
      bestFrame = static_cast<int>(trail.size()) - 1;
      bestSlot = 0;
      res.finalState = cands[0].st;
      if (opts.verbose && frame % 60 == 0)
        std::printf("  [beam] frame %5d  best %6.2f%%  beam %4d\n", frame,
                    record * 100.0f, keep);
    } else if (frame - recordFrame > opts.stallFrames) {
      if (opts.verbose)
        std::printf("  [beam] stalled at %.2f%% (frame %d)\n", record * 100.0f,
                    frame);
      break;
    }
  }
  res.progress = record;

reconstruct:
  if (trail.empty()) {
    // Nothing survived frame 0: the level kills the player where it spawns.
    res.frames = 0;
    res.holds.clear();
    return res;
  }
  // Defensive clamps: a bad index here is silent memory corruption, not a
  // crash at the point of the mistake.
  if (bestFrame >= static_cast<int>(trail.size()))
    bestFrame = static_cast<int>(trail.size()) - 1;
  if (bestFrame < 0) bestFrame = 0;
  if (bestSlot >= static_cast<int>(trail[bestFrame].size())) bestSlot = 0;
  res.holds.assign(bestFrame + 1, 0);
  int slot = bestSlot;
  for (int f = bestFrame; f >= 0; --f) {
    const auto& e = trail[f][slot];
    res.holds[f] = e.second;
    slot = e.first;
  }
  res.frames = static_cast<int>(res.holds.size());
  return res;
}

VerifyResult verifyMacro(const Level& level, const std::vector<uint8_t>& holds) {
  Sim sim(&level);
  VerifyResult vr;
  for (size_t i = 0; i < holds.size(); ++i) {
    if (!sim.step(holds[i] != 0)) break;
  }
  vr.progress = sim.progress();
  vr.solved = sim.state().won;
  vr.frames = sim.state().frame;
  return vr;
}

}  // namespace gd

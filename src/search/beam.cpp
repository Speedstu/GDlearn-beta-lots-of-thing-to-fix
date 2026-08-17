// SPDX-License-Identifier: MIT
#include "search/beam.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace gd {
namespace {

// Quantisation is deliberately finer than collision margins.  Vertical
// velocity is now expressed per 240-Hz tick, so its bucket is four times finer
// than the historical 60-Hz representation.  x also needs sub-unit precision
// because different speed-portal histories can reach the same tick at slightly
// different horizontal positions.
inline uint64_t stateKey(const State& s) {
  const int64_t qx = static_cast<int64_t>(std::lround(s.x * 2.0f));      // 0.5 u
  const int64_t qy = static_cast<int64_t>(std::lround(s.y * 2.0f));      // 0.5 u
  const int64_t qv = static_cast<int64_t>(std::lround(s.vy * 16.0f));    // 1/16 u/tick
  uint64_t h = 1469598103934665603ull;
  auto mix = [&h](uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
  };
  mix(static_cast<uint64_t>(qx + 1000000));
  mix(static_cast<uint64_t>(qy + 100000));
  mix(static_cast<uint64_t>(qv + 10000));
  mix(static_cast<uint64_t>(s.mode));
  mix(static_cast<uint64_t>(s.tier));
  mix((s.flip ? 1u : 0u) | (s.mini ? 2u : 0u) | (s.onGround ? 4u : 0u) |
      (s.holding ? 8u : 0u));
  mix(static_cast<uint64_t>(s.jumpHold));
  mix(static_cast<uint64_t>(s.lastOrbUid + 2));
  mix(static_cast<uint64_t>(s.lastPadUid + 2));
  return h;
}

// Trail entries used to be std::pair<int32_t,uint8_t>, which occupies 8 bytes
// because of padding.  The action fits in the high bit of a 32-bit parent
// index, halving the dominant memory cost on long 240-TPS demons.
constexpr uint32_t kActionBit = 0x80000000u;
constexpr uint32_t kParentMask = 0x7fffffffu;
inline uint32_t packTrail(int32_t parent, uint8_t action) {
  return (static_cast<uint32_t>(parent) & kParentMask) |
         (action ? kActionBit : 0u);
}

struct Cand {
  State st;
  float score = 0;
  float guide = 0;  // cumulative log-policy-likelihood
  int32_t parent = 0;
  uint8_t action = 0;
};

}  // namespace

SolveResult beamSolve(const Level& level, const SolveOptions& opts) {
  SolveResult res;
  Sim sim(&level);
  State root = sim.state();
  if (opts.hasStart) root = opts.start;

  std::vector<State> cur{root};
  std::vector<float> curGuide{0.0f};
  // trail[f][i] packs (parent slot in f-1, action taken to reach i).
  std::vector<std::vector<uint32_t>> trail;
  trail.reserve(2048);

  float record = std::max(0.0f, root.x / std::max(1.0f, level.length));
  int recordFrame = 0;
  int bestFrame = -1, bestSlot = 0;
  res.finalState = root;
  std::vector<Cand> cands;
  std::unordered_map<uint64_t, int> seen;
  std::vector<float> priorP;

  for (int frame = 0; frame < opts.maxFrames; ++frame) {
    cands.clear();
    cands.reserve(cur.size() * 2);
    seen.clear();
    seen.reserve(cur.size() * 4);

    priorP.clear();
    if (opts.priorWeight > 0.0f) {
      if (opts.priorBatch) {
        opts.priorBatch(cur, &priorP);
        if (priorP.size() != cur.size()) priorP.clear();
      } else if (opts.prior) {
        priorP.resize(cur.size());
        for (size_t i = 0; i < cur.size(); ++i) priorP[i] = opts.prior(cur[i]);
      }
    }

    for (int32_t i = 0; i < static_cast<int32_t>(cur.size()); ++i) {
      const bool guided = !priorP.empty();
      const float pHold = guided
          ? std::clamp(priorP[static_cast<size_t>(i)], 1e-5f, 1.0f - 1e-5f)
          : 0.5f;
      for (uint8_t a = 0; a < 2; ++a) {
        sim.restore(cur[static_cast<size_t>(i)]);
        sim.step(a != 0);
        res.expanded++;
        const State& ns = sim.state();
        if (ns.dead) continue;

        float guide = curGuide[static_cast<size_t>(i)];
        if (guided) guide += std::log(a ? pHold : 1.0f - pHold);

        // x matters when speed-portal histories diverge.  The height/ground
        // terms are deliberately weak: survival remains the hard constraint.
        float score = ns.x + 0.02f * (ns.y * ns.gdir()) +
                      (ns.onGround ? 0.5f : 0.0f);
        if (guided) {
          // Average path likelihood is stable across level length and prevents
          // a single uncertain decision from dominating thousands of ticks.
          score += opts.priorWeight * guide /
                   static_cast<float>(std::max(1, frame + 1));
        }

        if (ns.won) {
          trail.push_back({packTrail(i, a)});
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
          cands.push_back({ns, score, guide, i, a});
        } else if (cands[static_cast<size_t>(it->second)].score < score) {
          cands[static_cast<size_t>(it->second)] = {ns, score, guide, i, a};
        }
      }
    }

    if (cands.empty()) break;

    const int keep = std::min<int>(opts.beamWidth, static_cast<int>(cands.size()));
    std::partial_sort(cands.begin(), cands.begin() + keep, cands.end(),
                      [](const Cand& a, const Cand& b) {
                        return a.score > b.score;
                      });
    cands.resize(static_cast<size_t>(keep));

    cur.clear();
    curGuide.clear();
    cur.reserve(static_cast<size_t>(keep));
    curGuide.reserve(static_cast<size_t>(keep));
    std::vector<uint32_t> row;
    row.reserve(static_cast<size_t>(keep));
    for (const Cand& c : cands) {
      cur.push_back(c.st);
      curGuide.push_back(c.guide);
      row.push_back(packTrail(c.parent, c.action));
    }
    trail.push_back(std::move(row));

    // The best reconstruction target is the farthest surviving state, not
    // necessarily cands[0] (which may be ranked higher by the ML prior).
    int farthest = 0;
    for (int j = 1; j < keep; ++j)
      if (cur[static_cast<size_t>(j)].x > cur[static_cast<size_t>(farthest)].x)
        farthest = j;
    const float p = cur[static_cast<size_t>(farthest)].x /
                    std::max(1.0f, level.length);
    if (p > record + 1e-7f) {
      record = p;
      recordFrame = frame;
      bestFrame = static_cast<int>(trail.size()) - 1;
      bestSlot = farthest;
      res.finalState = cur[static_cast<size_t>(farthest)];
      if (opts.verbose && frame % phys::TPS == 0)
        std::printf("  [beam] tick %6d  best %6.2f%%  beam %5d\n", frame,
                    record * 100.0f, keep);
    } else if (frame - recordFrame > opts.stallFrames) {
      if (opts.verbose)
        std::printf("  [beam] stalled at %.2f%% (tick %d)\n", record * 100.0f,
                    frame);
      break;
    }
  }
  res.progress = record;

reconstruct:
  if (trail.empty() || bestFrame < 0) {
    res.frames = 0;
    res.holds.clear();
    return res;
  }
  if (bestFrame >= static_cast<int>(trail.size()))
    bestFrame = static_cast<int>(trail.size()) - 1;
  if (bestSlot >= static_cast<int>(trail[static_cast<size_t>(bestFrame)].size()))
    bestSlot = 0;

  res.holds.assign(static_cast<size_t>(bestFrame + 1), 0);
  int slot = bestSlot;
  for (int f = bestFrame; f >= 0; --f) {
    const uint32_t e = trail[static_cast<size_t>(f)][static_cast<size_t>(slot)];
    res.holds[static_cast<size_t>(f)] = (e & kActionBit) ? 1 : 0;
    slot = static_cast<int>(e & kParentMask);
    if (f > 0 && (slot < 0 || slot >= static_cast<int>(trail[static_cast<size_t>(f - 1)].size()))) {
      // Fail closed on an impossible corrupt trail rather than indexing random
      // memory and reporting a fake solver success.
      res.solved = false;
      res.holds.clear();
      res.frames = 0;
      return res;
    }
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

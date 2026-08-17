// SPDX-License-Identifier: MIT
// Deterministic beam search over the simulator.
//
// KEY INSIGHT the old project missed: Geometry Dash is a DETERMINISTIC,
// single-input, ~60 decisions-per-second game. That means an exact search can
// find a frame-perfect solution to a level without any learning at all, in
// seconds. RL is still useful (it generalises to unseen levels and to the
// real game's slightly different physics), but search is what gets you a
// 100%% run, and its solutions are the perfect demonstrations to train on.
//
// Memory note: only the current frame's states are kept in RAM. The path is
// reconstructed from a 5-bytes-per-node backpointer trail, so a 10k-frame
// search with a 2000-wide beam costs ~100 MB, not ~10 GB.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/level.hpp"
#include "core/sim.hpp"

namespace gd {

struct SolveOptions {
  int beamWidth = 800;
  int maxFrames = 60 * 180;      // 3 minutes of gameplay
  int stallFrames = 60 * 8;      // give up if the record stops improving
  bool verbose = true;
  // Optional policy prior: returns P(hold) in [0,1] for a state. When set,
  // nodes are scored by progress + priorWeight * log prior, which lets a
  // trained net guide the search (AlphaZero-style, minus the tree).
  std::function<float(const State&)> prior;
  float priorWeight = 0.0f;
  // Start the search from a specific state (used to resume from a failure).
  bool hasStart = false;
  State start{};
};

struct SolveResult {
  bool solved = false;
  float progress = 0;
  int frames = 0;
  int64_t expanded = 0;
  std::vector<uint8_t> holds;
  State finalState{};
};

SolveResult beamSolve(const Level& level, const SolveOptions& opts);

// Verifies a macro really beats the level in the simulator. Always run this
// before trusting a solution.
struct VerifyResult {
  bool solved = false;
  float progress = 0;
  int frames = 0;
};
VerifyResult verifyMacro(const Level& level, const std::vector<uint8_t>& holds);

}  // namespace gd

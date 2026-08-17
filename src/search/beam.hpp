// SPDX-License-Identifier: MIT
// Deterministic beam search over the native 240-TPS simulator.
//
// Geometry Dash is deterministic and single-input. Exact search therefore
// gives us replay-verifiable demonstrations; ML then distils/generalises those
// demonstrations and guides later searches. Search and learning are designed
// to reinforce each other rather than compete.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/level.hpp"
#include "core/sim.hpp"

namespace gd {

struct SolveOptions {
  int beamWidth = 800;
  int maxFrames = phys::ticks(180.0f);    // 3 minutes real time
  int stallFrames = phys::ticks(8.0f);    // no record improvement for 8 sec
  bool verbose = true;

  // Optional policy prior: returns P(hold) in [0,1] for a state. This is the
  // main bridge from ML back into exact search: a strong policy cheaply points
  // the beam toward human-like/high-probability branches while exact replay
  // verification remains the final authority.
  std::function<float(const State&)> prior;
  float priorWeight = 0.0f;

  bool hasStart = false;
  State start{};
};

struct SolveResult {
  bool solved = false;
  float progress = 0;
  int frames = 0;  // native physics ticks (legacy name kept for file/API compat)
  int64_t expanded = 0;
  std::vector<uint8_t> holds;
  State finalState{};
};

SolveResult beamSolve(const Level& level, const SolveOptions& opts);

struct VerifyResult {
  bool solved = false;
  float progress = 0;
  int frames = 0;
};
VerifyResult verifyMacro(const Level& level, const std::vector<uint8_t>& holds);

}  // namespace gd

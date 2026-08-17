// SPDX-License-Identifier: MIT
// Deterministic beam search over the native 240-TPS simulator.
//
// Geometry Dash is deterministic and single-input. Exact search gives us
// replay-verifiable demonstrations; ML then distils/generalises those demos and
// guides later searches. The batched prior interface is critical on demon-size
// beams: one NN forward per frontier, not one forward per state.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/level.hpp"
#include "core/sim.hpp"

namespace gd {

struct SolveOptions {
  int beamWidth = 800;
  int maxFrames = phys::ticks(180.0f);
  int stallFrames = phys::ticks(8.0f);
  bool verbose = true;

  // Scalar prior kept for simple callers/tests.
  std::function<float(const State&)> prior;
  // Preferred high-throughput prior. Must return one P(hold) per input state.
  std::function<void(const std::vector<State>&, std::vector<float>*)> priorBatch;
  // Ranking weight applied to average log-policy-likelihood of the complete
  // path so far. Zero keeps the solver purely model-free/exact-search driven.
  float priorWeight = 0.0f;

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

struct VerifyResult {
  bool solved = false;
  float progress = 0;
  int frames = 0;
};
VerifyResult verifyMacro(const Level& level, const std::vector<uint8_t>& holds);

}  // namespace gd

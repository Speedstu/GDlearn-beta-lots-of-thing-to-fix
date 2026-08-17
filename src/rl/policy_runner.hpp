// SPDX-License-Identifier: MIT
// Lightweight inference-only policy loader shared by search, evaluation and
// eventually the live Geometry Dash integration. No PPO/env allocation.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/level.hpp"
#include "core/sim.hpp"
#include "nn/net.hpp"
#include "rl/running.hpp"

namespace gd {

struct PolicyRollout {
  float progress = 0.0f;
  bool won = false;
  std::vector<uint8_t> holds;
  State finalState{};
};

class PolicyRunner {
 public:
  // Accepts both PPO and imitation/DAgger native-240 checkpoints.
  bool load(const std::string& dir, std::string* error = nullptr);

  // One P(hold) per state, using a single batched NN forward.
  void probabilities(const Level& level, const std::vector<State>& states,
                     std::vector<float>* pHold);

  // Deterministic greedy action for a single state.
  bool action(const Level& level, const State& state, float* pHold = nullptr);

  PolicyRollout evaluate(const Level& level,
                         int maxTicks = phys::ticks(180.0f));

  nn::Net& net() { return net_; }
  RunningNorm& norm() { return norm_; }
  int obsDimension() const { return obsDim_; }
  const std::string& schemaMagic() const { return schemaMagic_; }

 private:
  nn::Net net_;
  RunningNorm norm_;
  int obsDim_ = 0;
  std::string schemaMagic_;
  std::vector<float> obs_;
  std::vector<float> logits_;
  std::vector<float> values_;
};

// Shared schema writer for commands that train a policy outside Ppo.
bool writePolicySchema(const std::string& dir, const std::string& magic,
                       int obsDimension);

}  // namespace gd

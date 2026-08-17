// SPDX-License-Identifier: MIT
// PPO with the details that actually matter, and nothing else.
//
// Compared to the old ppo_agent.cpp this adds: vectorised envs collected in
// parallel with a frozen policy, GAE, per-minibatch advantage normalisation,
// value-loss clipping, entropy annealing, LR annealing, grad-norm clipping,
// observation normalisation, and checkpoints that round-trip.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/level.hpp"
#include "env/env.hpp"
#include "nn/net.hpp"
#include "rl/running.hpp"

namespace gd {

struct PpoConfig {
  int numEnvs = 64;
  int stepsPerEnv = 128;         // rollout = numEnvs * stepsPerEnv
  int epochs = 4;
  int minibatch = 2048;
  std::vector<int> hidden = {256, 256};

  float lr = 3e-4f;
  float gamma = 0.995f;          // GD attempts are long; 0.99 is too myopic
  float lambda = 0.95f;
  float clip = 0.2f;
  float valueCoef = 0.5f;
  float entropyStart = 0.02f;
  float entropyEnd = 0.002f;
  float maxGradNorm = 0.5f;
  bool annealLr = true;

  int64_t totalSteps = 20'000'000;
  int logEvery = 1;              // in updates
  int saveEvery = 25;            // in updates
  int threads = 0;               // 0 = hardware concurrency
  uint64_t seed = 1234;
  std::string outDir = "runs/default";
};

class Ppo {
 public:
  Ppo(const std::vector<Level>* pool, PpoConfig cfg);

  void train();

  nn::Net& net() { return net_; }
  RunningNorm& norm() { return norm_; }

  // Greedy (argmax) rollout of the current policy on one level.
  struct Rollout {
    float progress = 0;
    bool won = false;
    std::vector<uint8_t> holds;
  };
  Rollout evaluate(const Level& level, int maxFrames = 60 * 180,
                   bool stochastic = false);

  bool saveCheckpoint(const std::string& dir) const;
  bool loadCheckpoint(const std::string& dir);

 private:
  void collect();
  void update(float progressFrac);

  const std::vector<Level>* pool_;
  PpoConfig cfg_;
  nn::Net net_;
  RunningNorm norm_;
  EnvConfig envCfg_;
  std::vector<Env> envs_;
  int obsDim_ = 0;

  // Rollout buffers, layout [step][env].
  std::vector<float> obsBuf_, advBuf_, retBuf_, valBuf_, logpBuf_, rewBuf_;
  std::vector<uint8_t> actBuf_, doneBuf_;
  std::vector<float> lastObs_;   // [env][obsDim]

  int64_t stepsDone_ = 0;
  int updates_ = 0;

  // Rolling stats for logging.
  double epRetSum_ = 0;
  double epProgSum_ = 0;
  int epCount_ = 0;
  int winCount_ = 0;
  float bestProgress_ = 0;
};

}  // namespace gd

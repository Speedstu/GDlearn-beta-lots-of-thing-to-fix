// SPDX-License-Identifier: MIT
// PPO with vectorised collection, GAE, clipped objectives, running observation
// normalisation and resumable checkpoints. PPO is deliberately the finisher:
// exact search + DAgger provide rare-event competence first, then PPO improves
// robustness around those demonstrations.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/level.hpp"
#include "core/physics.hpp"
#include "env/env.hpp"
#include "nn/net.hpp"
#include "rl/running.hpp"

namespace gd {

struct PpoConfig {
  int numEnvs = 64;
  int stepsPerEnv = 128;
  int epochs = 4;
  int minibatch = 2048;
  std::vector<int> hidden = {256, 256};

  float lr = 3e-4f;
  float gamma = 0.995f;
  float lambda = 0.95f;
  float clip = 0.2f;
  float valueCoef = 0.5f;
  float entropyStart = 0.02f;
  float entropyEnd = 0.002f;
  float maxGradNorm = 0.5f;
  bool annealLr = true;

  int64_t totalSteps = 20'000'000;
  int logEvery = 1;
  int saveEvery = 25;
  int threads = 0;
  uint64_t seed = 1234;
  std::string outDir = "runs/default";
};

class Ppo {
 public:
  Ppo(const std::vector<Level>* pool, PpoConfig cfg);

  void train();

  nn::Net& net() { return net_; }
  RunningNorm& norm() { return norm_; }

  struct Rollout {
    float progress = 0;
    bool won = false;
    std::vector<uint8_t> holds;
  };
  Rollout evaluate(const Level& level, int maxFrames = phys::ticks(180.0f),
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

  std::vector<float> obsBuf_, advBuf_, retBuf_, valBuf_, logpBuf_, rewBuf_;
  std::vector<uint8_t> actBuf_, doneBuf_;
  std::vector<float> lastObs_;

  int64_t stepsDone_ = 0;
  int updates_ = 0;

  double epRetSum_ = 0;
  double epProgSum_ = 0;
  int epCount_ = 0;
  int winCount_ = 0;
  float bestProgress_ = 0;
};

}  // namespace gd

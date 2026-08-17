// SPDX-License-Identifier: MIT
// RL environment: observation + reward + episode bookkeeping, plus the two
// tricks that actually make hard GD levels learnable:
//
//   1. POTENTIAL-BASED progress reward. Reward is proportional to forward
//      distance, so it cannot be farmed by oscillating in place, and the
//      optimal policy is unchanged by the shaping (Ng et al. 1999).
//   2. GO-EXPLORE RESTARTS. Every attempt archives a snapshot per progress
//      bucket. New episodes start from a sampled frontier snapshot instead of
//      frame 0, so the agent gets thousands of tries at the wall it is stuck
//      on rather than replaying the first 20 seconds it already mastered.
//      This is the difference between "dies at 12%% forever" and "clears it".
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/level.hpp"
#include "core/rng.hpp"
#include "core/sim.hpp"
#include "env/obs.hpp"

namespace gd {

struct EnvConfig {
  float progressReward = 0.05f;   // per block of forward progress
  float deathPenalty = 1.0f;
  float winBonus = 10.0f;
  float timePenalty = 0.0f;       // per frame, usually 0 (speed is forced)
  int maxEpisodeFrames = 60 * 90;

  // Go-Explore
  bool goExplore = true;
  float restartProb = 0.60f;      // chance an episode starts from the archive
  int archiveBuckets = 200;       // progress resolution of the archive
  float frontierBias = 3.0f;      // >1 favours buckets near the record
};

struct StepInfo {
  bool done = false;
  bool won = false;
  bool died = false;
  float progress = 0;      // 0..1 at the end of the step
  float startProgress = 0; // where the episode began (for logging)
  int frames = 0;
};

class Env {
 public:
  Env(const std::vector<Level>* pool, EnvConfig cfg, uint64_t seed);

  void reset(float* obsOut);
  float step(bool hold, float* obsOut, StepInfo* info);

  const Sim& sim() const { return sim_; }
  float bestProgress() const { return best_; }
  int levelIndex() const { return levelIdx_; }
  // Highest progress reached from a cold start (not from an archive restart).
  float coldBest() const { return coldBest_; }

 private:
  void archivePush();
  bool sampleStart(State* out);

  const std::vector<Level>* pool_;
  EnvConfig cfg_;
  Rng rng_;
  Sim sim_;
  int levelIdx_ = 0;
  int epFrames_ = 0;
  float epStartProgress_ = 0;
  float best_ = 0;
  float coldBest_ = 0;
  bool fromArchive_ = false;

  // archive_[level][bucket] = snapshot, valid_ marks presence.
  std::vector<std::vector<State>> archive_;
  std::vector<std::vector<uint8_t>> valid_;
  std::vector<int> frontier_;  // highest filled bucket per level
};

}  // namespace gd

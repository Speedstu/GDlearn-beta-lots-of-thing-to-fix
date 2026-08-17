// SPDX-License-Identifier: MIT
#include "env/env.hpp"

#include <algorithm>
#include <cmath>

namespace gd {

Env::Env(const std::vector<Level>* pool, EnvConfig cfg, uint64_t seed)
    : pool_(pool), cfg_(cfg), rng_(seed) {
  archive_.assign(pool_->size(),
                  std::vector<State>(cfg_.archiveBuckets, State()));
  valid_.assign(pool_->size(), std::vector<uint8_t>(cfg_.archiveBuckets, 0));
  frontier_.assign(pool_->size(), 0);
  levelIdx_ = 0;
  sim_.setLevel(&(*pool_)[levelIdx_]);
}

void Env::reset(float* obsOut) {
  levelIdx_ = rng_.below(static_cast<int>(pool_->size()));
  sim_.setLevel(&(*pool_)[levelIdx_]);
  sim_.reset();

  fromArchive_ = false;
  State start;
  if (cfg_.goExplore && rng_.chance(cfg_.restartProb) && sampleStart(&start)) {
    sim_.restore(start);
    fromArchive_ = true;
  }
  epFrames_ = 0;
  epStartProgress_ = sim_.progress();
  encodeObs(sim_, obsOut);
}

bool Env::sampleStart(State* out) {
  const std::vector<uint8_t>& v = valid_[levelIdx_];
  const int top = frontier_[levelIdx_];
  if (top <= 0) return false;
  // Weight w(b) = (b/top)^frontierBias : mostly near the record, but still
  // revisits earlier sections so the policy does not forget them.
  float total = 0;
  for (int b = 0; b <= top; ++b) {
    if (!v[b]) continue;
    total += std::pow((static_cast<float>(b) + 1.0f) /
                          (static_cast<float>(top) + 1.0f),
                      cfg_.frontierBias);
  }
  if (total <= 0) return false;
  float pick = rng_.uniform() * total;
  for (int b = 0; b <= top; ++b) {
    if (!v[b]) continue;
    pick -= std::pow((static_cast<float>(b) + 1.0f) /
                         (static_cast<float>(top) + 1.0f),
                     cfg_.frontierBias);
    if (pick <= 0) {
      *out = archive_[levelIdx_][b];
      return true;
    }
  }
  return false;
}

void Env::archivePush() {
  if (!cfg_.goExplore) return;
  const float p = sim_.progress();
  int b = static_cast<int>(p * static_cast<float>(cfg_.archiveBuckets));
  b = std::clamp(b, 0, cfg_.archiveBuckets - 1);
  if (!valid_[levelIdx_][b]) {
    // Only archive states that are actually recoverable: standing on the
    // ground, or flying with a sane velocity. Archiving mid-death-spiral
    // states poisons the curriculum.
    const State& s = sim_.state();
    if (s.dead) return;
    archive_[levelIdx_][b] = s;
    valid_[levelIdx_][b] = 1;
    frontier_[levelIdx_] = std::max(frontier_[levelIdx_], b);
  }
}

float Env::step(bool hold, float* obsOut, StepInfo* info) {
  const float x0 = sim_.state().x;
  sim_.step(hold);
  const float dx = sim_.state().x - x0;

  float reward = cfg_.progressReward * (dx / phys::BLOCK) - cfg_.timePenalty;
  epFrames_++;

  const State& s = sim_.state();
  bool done = false;
  info->won = false;
  info->died = false;
  if (s.won) {
    reward += cfg_.winBonus;
    done = true;
    info->won = true;
  } else if (s.dead) {
    reward -= cfg_.deathPenalty;
    done = true;
    info->died = true;
  } else if (epFrames_ >= cfg_.maxEpisodeFrames) {
    done = true;  // timeout: no penalty, value bootstrapping handles it
  } else {
    archivePush();
  }

  const float p = sim_.progress();
  best_ = std::max(best_, p);
  if (!fromArchive_) coldBest_ = std::max(coldBest_, p);

  info->done = done;
  info->progress = p;
  info->startProgress = epStartProgress_;
  info->frames = epFrames_;

  if (done) {
    reset(obsOut);
  } else {
    encodeObs(sim_, obsOut);
  }
  return reward;
}

}  // namespace gd

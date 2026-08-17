// SPDX-License-Identifier: MIT
#include "rl/ppo.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <thread>

#include "core/rng.hpp"
#include "env/obs.hpp"

namespace gd {
namespace fs = std::filesystem;

Ppo::Ppo(const std::vector<Level>* pool, PpoConfig cfg)
    : pool_(pool), cfg_(cfg) {
  obsDim_ = obsDim();
  net_.build(obsDim_, cfg_.hidden, 2, cfg_.seed);
  norm_.init(obsDim_);
  envs_.reserve(cfg_.numEnvs);
  for (int i = 0; i < cfg_.numEnvs; ++i)
    envs_.emplace_back(pool_, envCfg_, cfg_.seed * 7919 + i * 104729 + 1);

  const size_t roll = static_cast<size_t>(cfg_.numEnvs) * cfg_.stepsPerEnv;
  obsBuf_.assign(roll * obsDim_, 0.0f);
  advBuf_.assign(roll, 0.0f);
  retBuf_.assign(roll, 0.0f);
  valBuf_.assign(roll + cfg_.numEnvs, 0.0f);
  logpBuf_.assign(roll, 0.0f);
  rewBuf_.assign(roll, 0.0f);
  actBuf_.assign(roll, 0);
  doneBuf_.assign(roll, 0);
  lastObs_.assign(static_cast<size_t>(cfg_.numEnvs) * obsDim_, 0.0f);

  for (int e = 0; e < cfg_.numEnvs; ++e)
    envs_[e].reset(lastObs_.data() + static_cast<size_t>(e) * obsDim_);
}

void Ppo::collect() {
  const int nEnv = cfg_.numEnvs, nStep = cfg_.stepsPerEnv;
  int threads = cfg_.threads > 0 ? cfg_.threads
                                 : static_cast<int>(std::thread::hardware_concurrency());
  threads = std::clamp(threads, 1, nEnv);

  auto worker = [&](int lo, int hi, uint64_t seed) {
    Rng rng(seed);
    const int shard = hi - lo;
    std::vector<float> obs(static_cast<size_t>(shard) * obsDim_);
    std::vector<float> logits, values, probs(2);
    double retSum = 0, progSum = 0;
    int eps = 0, wins = 0;
    float best = 0;

    for (int t = 0; t < nStep; ++t) {
      for (int e = lo; e < hi; ++e)
        std::memcpy(obs.data() + static_cast<size_t>(e - lo) * obsDim_,
                    lastObs_.data() + static_cast<size_t>(e) * obsDim_,
                    sizeof(float) * obsDim_);
      norm_.apply(obs.data(), shard);
      net_.forward(obs.data(), shard, &logits, &values, false);

      for (int e = lo; e < hi; ++e) {
        const int k = e - lo;
        const size_t slot = static_cast<size_t>(t) * nEnv + e;
        std::memcpy(obsBuf_.data() + slot * obsDim_,
                    obs.data() + static_cast<size_t>(k) * obsDim_,
                    sizeof(float) * obsDim_);
        nn::softmax(logits.data() + static_cast<size_t>(k) * 2, 2, probs.data());
        const int a = rng.uniform() < probs[1] ? 1 : 0;
        actBuf_[slot] = static_cast<uint8_t>(a);
        logpBuf_[slot] = std::log(std::max(probs[a], 1e-8f));
        valBuf_[slot] = values[k];

        StepInfo info;
        const float r = envs_[e].step(
            a != 0, lastObs_.data() + static_cast<size_t>(e) * obsDim_, &info);
        rewBuf_[slot] = r;
        doneBuf_[slot] = info.done ? 1 : 0;
        if (info.done) {
          eps++;
          retSum += r;
          progSum += info.progress;
          if (info.won) wins++;
          best = std::max(best, envs_[e].coldBest());
        }
      }
    }

    for (int e = lo; e < hi; ++e)
      std::memcpy(obs.data() + static_cast<size_t>(e - lo) * obsDim_,
                  lastObs_.data() + static_cast<size_t>(e) * obsDim_,
                  sizeof(float) * obsDim_);
    norm_.apply(obs.data(), shard);
    net_.forward(obs.data(), shard, &logits, &values, false);
    for (int e = lo; e < hi; ++e)
      valBuf_[static_cast<size_t>(nStep) * nEnv + e] = values[e - lo];

    return std::tuple<double, double, int, int, float>(retSum, progSum, eps,
                                                       wins, best);
  };

  std::vector<std::thread> pool;
  std::vector<std::tuple<double, double, int, int, float>> out(threads);
  const int per = (nEnv + threads - 1) / threads;
  for (int w = 0; w < threads; ++w) {
    const int lo = w * per, hi = std::min(nEnv, lo + per);
    if (lo >= hi) continue;
    pool.emplace_back([&, w, lo, hi] {
      out[w] = worker(lo, hi, cfg_.seed * 31 + updates_ * 1000003 + w);
    });
  }
  for (std::thread& th : pool) th.join();

  for (const auto& o : out) {
    epRetSum_ += std::get<0>(o);
    epProgSum_ += std::get<1>(o);
    epCount_ += std::get<2>(o);
    winCount_ += std::get<3>(o);
    bestProgress_ = std::max(bestProgress_, std::get<4>(o));
  }
  stepsDone_ += static_cast<int64_t>(nEnv) * nStep;
  const int normSamples = std::min(256, nEnv * nStep);
  if (normSamples > 0) norm_.observe(obsBuf_.data(), normSamples);
}

void Ppo::update(float progressFrac) {
  const int nEnv = cfg_.numEnvs, nStep = cfg_.stepsPerEnv;
  const size_t roll = static_cast<size_t>(nEnv) * nStep;

  std::vector<float> gae(nEnv, 0.0f);
  for (int t = nStep - 1; t >= 0; --t) {
    for (int e = 0; e < nEnv; ++e) {
      const size_t slot = static_cast<size_t>(t) * nEnv + e;
      const float nextV = (t == nStep - 1)
                              ? valBuf_[static_cast<size_t>(nStep) * nEnv + e]
                              : valBuf_[static_cast<size_t>(t + 1) * nEnv + e];
      const float notDone = doneBuf_[slot] ? 0.0f : 1.0f;
      const float delta =
          rewBuf_[slot] + cfg_.gamma * nextV * notDone - valBuf_[slot];
      gae[e] = delta + cfg_.gamma * cfg_.lambda * notDone * gae[e];
      advBuf_[slot] = gae[e];
      retBuf_[slot] = gae[e] + valBuf_[slot];
    }
  }

  const float lr = cfg_.annealLr ? cfg_.lr * (1.0f - progressFrac) : cfg_.lr;
  const float entCoef = cfg_.entropyStart +
                        (cfg_.entropyEnd - cfg_.entropyStart) * progressFrac;

  std::vector<size_t> order(roll);
  std::iota(order.begin(), order.end(), 0);
  Rng rng(cfg_.seed * 977 + updates_);
  for (size_t i = roll; i > 1; --i) {
    const size_t j = static_cast<size_t>(rng.next() % i);
    std::swap(order[i - 1], order[j]);
  }

  std::vector<float> mbObs, logits, values, dLogits, dValues, probs(2);
  for (int epoch = 0; epoch < cfg_.epochs; ++epoch) {
    for (size_t start = 0; start < roll; start += cfg_.minibatch) {
      const int mb = static_cast<int>(std::min<size_t>(cfg_.minibatch, roll - start));
      mbObs.assign(static_cast<size_t>(mb) * obsDim_, 0.0f);
      for (int i = 0; i < mb; ++i)
        std::memcpy(mbObs.data() + static_cast<size_t>(i) * obsDim_,
                    obsBuf_.data() + order[start + i] * obsDim_,
                    sizeof(float) * obsDim_);

      net_.zeroGrad();
      net_.forward(mbObs.data(), mb, &logits, &values, true);

      double m = 0, v = 0;
      for (int i = 0; i < mb; ++i) m += advBuf_[order[start + i]];
      m /= mb;
      for (int i = 0; i < mb; ++i) {
        const double d = advBuf_[order[start + i]] - m;
        v += d * d;
      }
      const float sd = static_cast<float>(std::sqrt(v / std::max(1, mb)) + 1e-8);

      dLogits.assign(static_cast<size_t>(mb) * 2, 0.0f);
      dValues.assign(mb, 0.0f);
      const float invMb = 1.0f / static_cast<float>(mb);

      for (int i = 0; i < mb; ++i) {
        const size_t s = order[start + i];
        nn::softmax(logits.data() + static_cast<size_t>(i) * 2, 2, probs.data());
        const int a = actBuf_[s];
        const float logp = std::log(std::max(probs[a], 1e-8f));
        const float ratio = std::exp(logp - logpBuf_[s]);
        const float adv = (advBuf_[s] - static_cast<float>(m)) / sd;

        const bool clipped =
            (adv >= 0 && ratio > 1.0f + cfg_.clip) ||
            (adv < 0 && ratio < 1.0f - cfg_.clip);
        float dLogp = clipped ? 0.0f : -adv * ratio;

        for (int k = 0; k < 2; ++k) {
          const float dl = ((k == a ? 1.0f : 0.0f) - probs[k]);
          dLogits[static_cast<size_t>(i) * 2 + k] += dLogp * dl * invMb;
        }
        float H = 0;
        for (int k = 0; k < 2; ++k)
          H -= probs[k] * std::log(std::max(probs[k], 1e-8f));
        for (int k = 0; k < 2; ++k) {
          const float lp = std::log(std::max(probs[k], 1e-8f));
          const float dH = -probs[k] * (lp + H);
          dLogits[static_cast<size_t>(i) * 2 + k] += -entCoef * dH * invMb;
        }
        const float vPred = values[i];
        const float vOld = valBuf_[s];
        const float vClipped =
            std::clamp(vPred, vOld - cfg_.clip * 10.0f, vOld + cfg_.clip * 10.0f);
        const float e1 = vPred - retBuf_[s];
        const float e2 = vClipped - retBuf_[s];
        const float useClipped = (e2 * e2 > e1 * e1) ? 1.0f : 0.0f;
        const float err = useClipped ? e2 : e1;
        dValues[i] = cfg_.valueCoef * 2.0f * err * invMb;
      }

      net_.backward(mbObs.data(), mb, dLogits.data(), dValues.data());
      net_.clipGradNorm(cfg_.maxGradNorm);
      net_.adamStep(lr);
    }
  }
  updates_++;
}

void Ppo::train() {
  const int64_t perUpdate = static_cast<int64_t>(cfg_.numEnvs) * cfg_.stepsPerEnv;
  const int totalUpdates = static_cast<int>(cfg_.totalSteps / perUpdate) + 1;

  std::printf("PPO | obs=%d params=%d envs=%d rollout=%lld updates=%d\n",
              obsDim_, net_.paramCount(), cfg_.numEnvs,
              static_cast<long long>(perUpdate), totalUpdates);
  std::string csvPath = cfg_.outDir + "/train_log.csv";
  FILE* csv = std::fopen(csvPath.c_str(), "w");
  if (csv)
    std::fprintf(csv,
                 "update,steps,episodes,mean_progress,win_rate,best_cold_progress\n");

  for (int u = 0; u < totalUpdates; ++u) {
    const float frac = static_cast<float>(u) / static_cast<float>(totalUpdates);
    collect();
    update(frac);

    if (u % cfg_.logEvery == 0) {
      const double meanProg = epCount_ ? epProgSum_ / epCount_ : 0.0;
      const double winRate = epCount_ ? static_cast<double>(winCount_) / epCount_ : 0.0;
      std::printf(
          "u%-5d steps %-10lld eps %-6d mean %6.2f%%  win %5.1f%%  record %6.2f%%\n",
          u, static_cast<long long>(stepsDone_), epCount_, meanProg * 100.0,
          winRate * 100.0, bestProgress_ * 100.0);
      std::fflush(stdout);
      if (csv) {
        std::fprintf(csv, "%d,%lld,%d,%.5f,%.5f,%.5f\n", u,
                     static_cast<long long>(stepsDone_), epCount_, meanProg,
                     winRate, bestProgress_);
        std::fflush(csv);
      }
      epRetSum_ = epProgSum_ = 0;
      epCount_ = winCount_ = 0;
    }
    if (cfg_.saveEvery > 0 && u > 0 && u % cfg_.saveEvery == 0)
      saveCheckpoint(cfg_.outDir);
  }
  saveCheckpoint(cfg_.outDir);
  if (csv) std::fclose(csv);
}

Ppo::Rollout Ppo::evaluate(const Level& level, int maxFrames, bool stochastic) {
  Rollout out;
  Sim sim(&level);
  std::vector<float> obs(obsDim_), logits, values, probs(2);
  Rng rng(12345);
  for (int f = 0; f < maxFrames; ++f) {
    encodeObs(sim, obs.data());
    norm_.apply(obs.data(), 1);
    net_.forward(obs.data(), 1, &logits, &values, false);
    nn::softmax(logits.data(), 2, probs.data());
    const bool hold =
        stochastic ? (rng.uniform() < probs[1]) : (probs[1] > 0.5f);
    out.holds.push_back(hold ? 1 : 0);
    if (!sim.step(hold)) break;
  }
  out.progress = sim.progress();
  out.won = sim.state().won;
  return out;
}

bool Ppo::saveCheckpoint(const std::string& dir) const {
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) return false;
  if (!net_.save(dir + "/policy.bin") || !norm_.save(dir + "/obs_norm.bin"))
    return false;
  std::ofstream meta(dir + "/schema.txt", std::ios::trunc);
  if (!meta) return false;
  meta << "gdlearn_policy_v2\n";
  meta << "tps " << phys::TPS << "\n";
  meta << "obs_dim " << obsDim_ << "\n";
  return static_cast<bool>(meta);
}

bool Ppo::loadCheckpoint(const std::string& dir) {
  std::ifstream meta(dir + "/schema.txt");
  if (!meta) return false;
  std::string magic;
  std::getline(meta, magic);
  if (magic != "gdlearn_policy_v2" && magic != "gdlearn_imitation_v2") return false;
  int tps = -1, dim = -1;
  std::string key;
  while (meta >> key) {
    if (key == "tps") meta >> tps;
    else if (key == "obs_dim") meta >> dim;
    else {
      std::string ignored;
      std::getline(meta, ignored);
    }
  }
  if (tps != phys::TPS || dim != obsDim_) return false;
  const bool a = net_.load(dir + "/policy.bin");
  const bool b = norm_.load(dir + "/obs_norm.bin");
  return a && b;
}

}  // namespace gd

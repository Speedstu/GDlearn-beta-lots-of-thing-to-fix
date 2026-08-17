// SPDX-License-Identifier: MIT
#include "rl/curriculum.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/physics.hpp"
#include "env/obs.hpp"
#include "io/macro.hpp"
#include "nn/net.hpp"
#include "rl/running.hpp"
#include "search/beam.hpp"

namespace gd {
namespace fs = std::filesystem;

namespace {

std::string stemOf(const std::string& path) {
  return fs::path(path).stem().string();
}

std::vector<std::string> collectLevelPaths(const std::vector<std::string>& roots) {
  std::vector<std::string> out;
  for (const std::string& root : roots) {
    if (!fs::exists(root)) continue;
    if (fs::is_regular_file(root) && fs::path(root).extension() == ".gdl") {
      out.push_back(root);
      continue;
    }
    if (!fs::is_directory(root)) continue;
    for (const auto& e : fs::recursive_directory_iterator(root))
      if (e.is_regular_file() && e.path().extension() == ".gdl")
        out.push_back(e.path().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

float difficultyScore(const RatedLevel& r) {
  const float beamTerm = std::log2(static_cast<float>(std::max(2, r.beamNeeded)));
  const float expandedPerFrame =
      r.frames > 0 ? static_cast<float>(r.expanded) / static_cast<float>(r.frames)
                   : 0.0f;
  return 10.0f * beamTerm + 0.004f * expandedPerFrame +
         1.5f * r.objectsPerBlock + 0.004f * r.lengthBlocks;
}

}  // namespace

std::vector<uint8_t> minimizeMacro(const Level& level,
                                  std::vector<uint8_t> holds,
                                  const State* start) {
  auto beats = [&](const std::vector<uint8_t>& h) {
    Sim sim(&level);
    if (start) sim.restore(*start);
    for (size_t f = 0; f < h.size() && sim.alive(); ++f) sim.step(h[f] != 0);
    return sim.state().won;
  };
  if (!beats(holds)) return holds;
  for (int f = static_cast<int>(holds.size()) - 1; f >= 0; --f) {
    if (!holds[f]) continue;
    holds[f] = 0;
    if (!beats(holds)) holds[f] = 1;
  }
  return holds;
}

std::vector<RatedLevel> rateLibrary(const CurriculumConfig& cfg) {
  std::vector<std::string> paths = collectLevelPaths(cfg.levelDirs);

  // Macro cache keys are stems for backwards compatibility. Two distinct
  // files named e.g. easy/1.gdl and demon/1.gdl must never share an oracle.
  // Fail closed instead of training on a replay that belongs to another level.
  {
    std::unordered_map<std::string, std::string> seen;
    for (const std::string& path : paths) {
      const std::string stem = stemOf(path);
      auto [it, inserted] = seen.emplace(stem, path);
      if (!inserted && it->second != path) {
        std::printf("ERROR: duplicate level stem '%s':\n  %s\n  %s\n"
                    "Use unique filenames so macro/checkpoint caches cannot collide.\n",
                    stem.c_str(), it->second.c_str(), path.c_str());
        return {};
      }
    }
  }

  // Procedural levels widen the bottom of the ramp so the policy has something
  // it can actually clear before meeting a real level.
  std::vector<Level> generated;
  if (cfg.genUpTo >= 0) {
    for (int d = 0; d <= cfg.genUpTo; ++d)
      for (int s = 0; s < cfg.genPer; ++s)
        generated.push_back(makeTrainingLevel(d, static_cast<uint64_t>(s) + 1));
  }

  const int total = static_cast<int>(paths.size() + generated.size());
  std::vector<RatedLevel> rated(static_cast<size_t>(std::max(0, total)));
  if (total == 0) return rated;

  if (!cfg.macroDir.empty()) fs::create_directories(cfg.macroDir);
  const int minBeam = cfg.rateBeams.empty() ? 400 : cfg.rateBeams.front();

  std::atomic<int> cursor{0};
  std::mutex ioMutex;
  std::atomic<int> done{0};

  auto worker = [&]() {
    for (;;) {
      const int i = cursor.fetch_add(1);
      if (i >= total) return;

      RatedLevel r;
      Level lv;
      if (i < static_cast<int>(paths.size())) {
        r.path = paths[static_cast<size_t>(i)];
        lv = Level::loadGdl(r.path);
      } else {
        lv = generated[static_cast<size_t>(i) - paths.size()];
        r.path = "";  // procedural, not on disk
      }
      r.name = lv.name.empty() ? stemOf(r.path) : lv.name;
      if (!r.path.empty()) r.name = stemOf(r.path);
      r.lengthBlocks = lv.length / phys::BLOCK;
      r.objectsPerBlock =
          r.lengthBlocks > 0 ? static_cast<float>(lv.objectCount()) / r.lengthBlocks
                             : 0.0f;

      // A cached macro from a previous run saves minutes per level.
      const std::string macroPath =
          cfg.macroDir.empty() ? std::string()
                               : cfg.macroDir + "/" + r.name + ".macro";
      bool cached = false;
      try {
      if (cfg.reuseMacros && !macroPath.empty() && fs::exists(macroPath)) {
        Macro m;
        if (Macro::load(macroPath, &m) && verifyMacro(lv, m.holds).solved) {
          r.oracle = m.holds;
          r.solved = true;
          r.progress = 1.0f;
          r.frames = static_cast<int>(m.holds.size());
          r.beamNeeded = minBeam;   // unknown; assume the cheap end
          r.expanded = 0;
          cached = true;
        }
      }

      if (!cached) {
        // Estimate the native-tick horizon from level length at the slowest
        // horizontal speed, then add 10 seconds for portals/ship sections.
        // At 240 TPS this horizon is ~4x the old frame count, so using one
        // global maximum for every level would massively over-allocate trails.
        const float perTick = phys::SPEEDS[0] * phys::DT;
        int frameCap =
            static_cast<int>(lv.length / std::max(0.001f, perTick) * 1.4f) +
            phys::ticks(10.0f);
        if (frameCap > cfg.rateMaxFrames) frameCap = cfg.rateMaxFrames;

        // beam.cpp packs (parent, action) into one uint32_t = 4 bytes/tick.
        // Trail history dominates long searches; 2.5x leaves headroom for the
        // live frontier, candidate states, hash table and allocator overhead.
        // This is intentionally conservative but no longer assumes the old
        // padded 8-byte pair backpointer.
        constexpr double kPackedTrailBytes = 4.0;
        constexpr double kSearchHeadroom = 2.5;
        const double bytesPerSlot = kPackedTrailBytes * kSearchHeadroom;
        int beamCap = static_cast<int>(static_cast<double>(cfg.memBudgetMb) *
                                      1024.0 * 1024.0 /
                                      (bytesPerSlot * std::max(1, frameCap)));
        if (beamCap < 200) beamCap = 200;

        if (cfg.announce) {
          std::lock_guard<std::mutex> lock(ioMutex);
          std::printf("  ... rating %-24s %4.0f blocks, %d ticks, beam <= %d\n",
                      r.name.c_str(), r.lengthBlocks, frameCap, beamCap);
          std::fflush(stdout);
        }
        for (int wanted : cfg.rateBeams) {
          const int beam = std::min(wanted, beamCap);
          SolveOptions o;
          o.beamWidth = beam;
          o.maxFrames = frameCap;
          o.stallFrames = cfg.rateStall;
          o.verbose = false;
          SolveResult sr = beamSolve(lv, o);
          r.beamNeeded = beam;
          r.frames = sr.frames;
          r.expanded = sr.expanded;
          r.progress = std::max(r.progress, sr.progress);
          if (sr.solved) {
            r.solved = true;
            r.progress = 1.0f;
            r.oracle = minimizeMacro(lv, sr.holds, nullptr);
            if (!macroPath.empty()) {
              Macro m;
              m.level = r.name;
              m.progress = 1.0f;
              m.holds = r.oracle;
              m.save(macroPath);
            }
            break;
          }
        }
      }
      } catch (const std::exception& e) {
        r.error = e.what();
      } catch (...) {
        r.error = "unknown exception";
      }
      r.score = difficultyScore(r);
      rated[static_cast<size_t>(i)] = std::move(r);
      const int n = done.fetch_add(1) + 1;
      if (cfg.announce) {
        std::lock_guard<std::mutex> lock(ioMutex);
        const RatedLevel& rr = rated[static_cast<size_t>(i)];
        std::printf("  [%d/%d] %-24s %6.2f%% beam=%d%s%s\n", n, total,
                    rr.name.c_str(), rr.progress * 100.0f, rr.beamNeeded,
                    rr.solved ? " SOLVED" : "",
                    rr.error.empty() ? "" : " ERROR");
        std::fflush(stdout);
      }
    }
  };

  int threads = cfg.threads > 0 ? cfg.threads
                                : static_cast<int>(std::thread::hardware_concurrency());
  threads = std::clamp(threads, 1, total);
  std::vector<std::thread> workers;
  workers.reserve(static_cast<size_t>(threads));
  for (int i = 0; i < threads; ++i) workers.emplace_back(worker);
  for (std::thread& t : workers) t.join();

  std::stable_sort(rated.begin(), rated.end(), [](const RatedLevel& a,
                                                  const RatedLevel& b) {
    if (a.solved != b.solved) return a.solved > b.solved;
    if (a.solved && b.solved) return a.score < b.score;
    return a.progress > b.progress;
  });
  return rated;
}

namespace {

class Trainer {
 public:
  Trainer(const CurriculumConfig& cfg, int obsDimension)
      : cfg_(cfg), D_(obsDimension) {
    net_.build(D_, {cfg.hidden, cfg.hidden}, 2, cfg.seed);
    norm_.init(D_);
  }

  void add(const Level& level, const std::vector<uint8_t>& holds,
           const State* start = nullptr) {
    Sim sim(&level);
    if (start) sim.restore(*start);
    std::vector<float> row(static_cast<size_t>(D_));
    for (uint8_t hold : holds) {
      if (!sim.alive()) break;
      encodeObs(sim, row.data());
      X_.insert(X_.end(), row.begin(), row.end());
      Y_.push_back(hold ? 1 : 0);
      sim.step(hold != 0);
    }
    trim();
  }

  struct FitStats { double loss = 0, accuracy = 0; };

  FitStats train(int epochs) {
    FitStats stats;
    const int n = static_cast<int>(Y_.size());
    if (n == 0) return stats;
    norm_.init(D_);
    norm_.observe(X_.data(), n);
    std::vector<float> xn = X_;
    norm_.apply(xn.data(), n);

    std::vector<int> idx(static_cast<size_t>(n));
    std::iota(idx.begin(), idx.end(), 0);
    std::mt19937 rng(static_cast<uint32_t>(cfg_.seed + trainCalls_++));
    std::vector<float> batch, logits, values, probs, dlogits, dvalues;

    int64_t nHold = 0;
    for (uint8_t y : Y_) nHold += y ? 1 : 0;
    const int64_t nRel = n - nHold;
    float w[2] = {1.0f, 1.0f};
    if (nHold > 0 && nRel > 0) {
      w[0] = 0.5f * static_cast<float>(n) / static_cast<float>(nRel);
      w[1] = 0.5f * static_cast<float>(n) / static_cast<float>(nHold);
    }

    for (int epoch = 0; epoch < epochs; ++epoch) {
      std::shuffle(idx.begin(), idx.end(), rng);
      double lossSum = 0.0;
      int64_t correct = 0, seen = 0;
      for (int at = 0; at < n; at += cfg_.minibatch) {
        const int bs = std::min(cfg_.minibatch, n - at);
        batch.resize(static_cast<size_t>(bs) * D_);
        for (int b = 0; b < bs; ++b)
          std::memcpy(batch.data() + static_cast<size_t>(b) * D_,
                      xn.data() + static_cast<size_t>(idx[at + b]) * D_,
                      sizeof(float) * D_);

        net_.forward(batch.data(), bs, &logits, &values, true);
        probs.resize(static_cast<size_t>(bs) * 2);
        dlogits.assign(static_cast<size_t>(bs) * 2, 0.0f);
        dvalues.assign(static_cast<size_t>(bs), 0.0f);
        for (int b = 0; b < bs; ++b) {
          nn::softmax(logits.data() + static_cast<size_t>(b) * 2, 2,
                      probs.data() + static_cast<size_t>(b) * 2);
          const int label = Y_[static_cast<size_t>(idx[at + b])];
          const float wl = w[label];
          lossSum += -wl * std::log(std::max(1e-8f,
              probs[static_cast<size_t>(b) * 2 + label]));
          const int pred = probs[static_cast<size_t>(b) * 2 + 1] >
                                   probs[static_cast<size_t>(b) * 2]
                               ? 1 : 0;
          correct += pred == label ? 1 : 0;
          seen++;
          for (int k = 0; k < 2; ++k)
            dlogits[static_cast<size_t>(b) * 2 + k] =
                wl * (probs[static_cast<size_t>(b) * 2 + k] -
                      (k == label ? 1.0f : 0.0f)) /
                static_cast<float>(bs);
        }
        net_.zeroGrad();
        net_.backward(batch.data(), bs, dlogits.data(), dvalues.data());
        net_.clipGradNorm(cfg_.maxGradNorm);
        net_.adamStep(cfg_.lr);
      }
      stats.loss = seen ? lossSum / static_cast<double>(seen) : 0.0;
      stats.accuracy = seen ? static_cast<double>(correct) / static_cast<double>(seen)
                            : 0.0;
    }
    return stats;
  }

  State rollout(const Level& level, std::vector<State>* trace,
                std::vector<uint8_t>* holds) {
    Sim sim(&level);
    std::vector<float> obs(static_cast<size_t>(D_)), logits, values;
    for (int f = 0; f < cfg_.evalMaxFrames && sim.alive(); ++f) {
      if (trace) trace->push_back(sim.state());
      encodeObs(sim, obs.data());
      norm_.apply(obs.data(), 1);
      net_.forward(obs.data(), 1, &logits, &values, false);
      const bool hold = logits[1] > logits[0];
      if (holds) holds->push_back(hold ? 1 : 0);
      sim.step(hold);
    }
    return sim.state();
  }

  bool loadModel(const std::string& dir) {
    std::ifstream meta(dir + "/schema.txt");
    if (!meta) return false;
    std::string magic;
    std::getline(meta, magic);
    if (magic != "gdlearn_imitation_v2" && magic != "gdlearn_policy_v2") return false;
    int tps = -1, dim = -1;
    std::string key;
    while (meta >> key) {
      if (key == "tps") meta >> tps;
      else if (key == "obs_dim") meta >> dim;
      else { std::string ignored; std::getline(meta, ignored); }
    }
    if (tps != phys::TPS || dim != D_) return false;
    return net_.load(dir + "/policy.bin") && norm_.load(dir + "/obs_norm.bin");
  }

  void saveModel(const std::string& dir) const {
    fs::create_directories(dir);
    net_.save(dir + "/policy.bin");
    norm_.save(dir + "/obs_norm.bin");
    std::ofstream meta(dir + "/schema.txt", std::ios::trunc);
    meta << "gdlearn_imitation_v2\n"
         << "tps " << phys::TPS << "\n"
         << "obs_dim " << D_ << "\n";
  }

  bool loadFrom(const std::string& dir) {
    if (!loadModel(dir)) return false;
    std::ifstream in(dir + "/dagger_dataset.bin", std::ios::binary);
    if (!in) return true;  // checkpoint is still valid; demonstrations rebuild
    uint32_t magic = 0;
    int32_t dim = 0;
    uint64_t n = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&dim), sizeof(dim));
    in.read(reinterpret_cast<char*>(&n), sizeof(n));
    const uint64_t hardCap = cfg_.maxSamples > 0
                                 ? static_cast<uint64_t>(cfg_.maxSamples)
                                 : 2'000'000ull;
    if (!in || magic != 0x47444432u || dim != D_ || n > hardCap) return false;
    X_.resize(static_cast<size_t>(n) * static_cast<size_t>(D_));
    Y_.resize(static_cast<size_t>(n));
    in.read(reinterpret_cast<char*>(X_.data()),
            static_cast<std::streamsize>(X_.size() * sizeof(float)));
    in.read(reinterpret_cast<char*>(Y_.data()),
            static_cast<std::streamsize>(Y_.size() * sizeof(uint8_t)));
    if (!in) { X_.clear(); Y_.clear(); return false; }
    return true;
  }

  void saveTo(const std::string& dir) const {
    saveModel(dir);
    std::ofstream out(dir + "/dagger_dataset.bin",
                      std::ios::binary | std::ios::trunc);
    if (!out) return;
    const uint32_t magic = 0x47444432u;  // GDD2
    const int32_t dim = D_;
    const uint64_t n = static_cast<uint64_t>(Y_.size());
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&dim), sizeof(dim));
    out.write(reinterpret_cast<const char*>(&n), sizeof(n));
    out.write(reinterpret_cast<const char*>(X_.data()),
              static_cast<std::streamsize>(X_.size() * sizeof(float)));
    out.write(reinterpret_cast<const char*>(Y_.data()),
              static_cast<std::streamsize>(Y_.size() * sizeof(uint8_t)));
  }

  int samples() const { return static_cast<int>(Y_.size()); }

 private:
  void trim() {
    if (cfg_.maxSamples <= 0 || static_cast<int>(Y_.size()) <= cfg_.maxSamples)
      return;
    const size_t keep = static_cast<size_t>(cfg_.maxSamples);
    const size_t drop = Y_.size() - keep;
    std::vector<float> nx(X_.begin() + static_cast<ptrdiff_t>(drop * D_), X_.end());
    std::vector<uint8_t> ny(Y_.begin() + static_cast<ptrdiff_t>(drop), Y_.end());
    X_.swap(nx);
    Y_.swap(ny);
  }

  const CurriculumConfig& cfg_;
  int D_ = 0;
  nn::Net net_;
  RunningNorm norm_;
  std::vector<float> X_;
  std::vector<uint8_t> Y_;
  int trainCalls_ = 0;
};

Level loadRatedLevel(const RatedLevel& r) {
  if (!r.path.empty()) return Level::loadGdl(r.path);
  // Generated paths are empty. Their names are "proc_d<d>_s<s>".
  int d = 0, s = 0;
  if (std::sscanf(r.name.c_str(), "proc_d%d_s%d", &d, &s) == 2)
    return makeTrainingLevel(d, static_cast<uint64_t>(s) + 1);
  throw std::runtime_error("cannot reconstruct procedural level " + r.name);
}

}  // namespace

int runCurriculum(const CurriculumConfig& cfg) {
  std::vector<RatedLevel> rated = rateLibrary(cfg);
  if (rated.empty()) {
    std::printf("no levels found\n");
    return 2;
  }

  if (cfg.rateOnly) {
    std::printf("\n%-4s %-26s %-9s %-7s %-8s %-10s\n", "#", "level", "progress",
                "beam", "frames", "score");
    for (size_t i = 0; i < rated.size(); ++i) {
      const RatedLevel& r = rated[i];
      std::printf("%-4zu %-26s %7.2f%% %-7d %-8d %-10.2f%s\n", i + 1,
                  r.name.c_str(), r.progress * 100.0f, r.beamNeeded, r.frames,
                  r.score, r.solved ? " SOLVED" : "");
    }
    return 0;
  }

  std::vector<RatedLevel> useable;
  for (RatedLevel& r : rated) {
    if (!r.solved) continue;
    Level lv = loadRatedLevel(r);
    if (!verifyMacro(lv, r.oracle).solved) continue;
    useable.push_back(std::move(r));
  }
  if (useable.empty()) {
    std::printf("no replay-verified solved levels available for training\n");
    return 3;
  }

  fs::create_directories(cfg.outDir);
  Trainer trainer(cfg, obsDim());
  if (cfg.resume && trainer.loadFrom(cfg.outDir))
    std::printf("resumed native-240 policy + %d DAgger samples from %s\n",
                trainer.samples(), cfg.outDir.c_str());
  else if (cfg.resume)
    std::printf("resume requested but no compatible 240-TPS checkpoint found; starting clean\n");

  std::ofstream log(cfg.outDir + "/curriculum_log.csv");
  log << "tier,round,active,samples,loss,accuracy,mean_progress,cleared\n";

  std::vector<Level> levels;
  levels.reserve(useable.size());
  for (RatedLevel& r : useable) {
    Level lv = loadRatedLevel(r);
    levels.push_back(lv);
    trainer.add(levels.back(), r.oracle);
  }

  const int n = static_cast<int>(levels.size());
  const int tiers = std::max(1, std::min(cfg.tiers, n));
  int active = 0;   // number of levels unlocked so far

  auto scorePolicy = [&](int count) {
    double sum = 0.0;
    int wins = 0;
    for (int i = 0; i < count; ++i) {
      const State end = trainer.rollout(levels[static_cast<size_t>(i)], nullptr, nullptr);
      const float prog = end.won ? 1.0f
          : std::min(1.0f, end.x / std::max(1.0f, levels[static_cast<size_t>(i)].length));
      sum += prog;
      wins += end.won ? 1 : 0;
    }
    return std::pair<int, double>{wins, sum / std::max(1, count)};
  };

  for (int tier = 0; tier < tiers; ++tier) {
    const int target = std::max(active + 1,
        static_cast<int>(std::ceil(static_cast<double>(n) * (tier + 1) / tiers)));
    active = std::min(n, target);

    std::printf("\n=== tier %d/%d: %d level(s) unlocked, hardest so far '%s' ===\n",
                tier + 1, tiers, active,
                useable[static_cast<size_t>(active - 1)].name.c_str());

    const std::string guardDir = cfg.outDir + "/.champion_guard";
    trainer.saveModel(guardDir);
    auto [bestCleared, bestMean] = scorePolicy(active);
    std::printf("  protected baseline: mean %.2f%% cleared %d/%d\n",
                bestMean * 100.0, bestCleared, active);

    for (int round = 0; round < cfg.roundsPerTier; ++round) {
      Trainer::FitStats fit = trainer.train(cfg.epochs);
      int cleared = 0;
      double meanProg = 0.0;
      std::vector<State> deaths(static_cast<size_t>(active));
      std::vector<std::vector<State>> traces(static_cast<size_t>(active));
      for (int i = 0; i < active; ++i) {
        State end = trainer.rollout(levels[static_cast<size_t>(i)],
                                    &traces[static_cast<size_t>(i)], nullptr);
        deaths[static_cast<size_t>(i)] = end;
        const float prog = end.won ? 1.0f
            : std::min(1.0f, end.x / std::max(1.0f, levels[static_cast<size_t>(i)].length));
        meanProg += prog;
        cleared += end.won ? 1 : 0;
      }
      meanProg /= std::max(1, active);

      std::printf("  round %d: loss %.4f acc %.1f%% mean %.2f%% cleared %d/%d samples %d\n",
                  round + 1, fit.loss, fit.accuracy * 100.0, meanProg * 100.0,
                  cleared, active, trainer.samples());
      log << tier << ',' << round << ',' << active << ',' << trainer.samples()
          << ',' << fit.loss << ',' << fit.accuracy << ',' << meanProg << ','
          << cleared << '\n';
      log.flush();

      const bool regressed = cleared < bestCleared ||
          (cleared == bestCleared && meanProg + 1e-9 < bestMean);
      if (regressed) {
        std::printf("    regression vs protected champion (%.2f%%/%d -> %.2f%%/%d); restoring\n",
                    bestMean * 100.0, bestCleared, meanProg * 100.0, cleared);
        if (!trainer.loadModel(guardDir))
          throw std::runtime_error("failed to restore protected curriculum champion");
        continue;
      }
      if (cleared > bestCleared || meanProg > bestMean + 1e-9) {
        bestCleared = cleared;
        bestMean = meanProg;
        trainer.saveModel(guardDir);
      }

      if (cleared >= static_cast<int>(std::ceil(cfg.promote * active))) {
        std::printf("    promotion threshold met\n");
        break;
      }

      // DAgger rescue: for each failed policy rollout, branch exact search from
      // increasingly earlier points in its trace. A successful continuation is
      // an on-distribution corrective demonstration.
      for (int i = 0; i < active; ++i) {
        if (deaths[static_cast<size_t>(i)].won) continue;
        auto& trace = traces[static_cast<size_t>(i)];
        for (int back : {phys::ticks(0.5f), phys::ticks(1.5f),
                       phys::ticks(4.0f), phys::ticks(10.0f)}) {
          if (trace.empty()) break;
          const int at = std::max(0, static_cast<int>(trace.size()) - 1 - back);
          SolveOptions so;
          so.beamWidth = cfg.rescueBeam;
          so.maxFrames = cfg.evalMaxFrames;
          so.stallFrames = phys::ticks(10.0f);
          so.verbose = false;
          so.hasStart = true;
          so.start = trace[static_cast<size_t>(at)];
          SolveResult sr = beamSolve(levels[static_cast<size_t>(i)], so);
          if (!sr.solved) continue;
          std::vector<uint8_t> correction =
              minimizeMacro(levels[static_cast<size_t>(i)], sr.holds,
                            &trace[static_cast<size_t>(at)]);
          trainer.add(levels[static_cast<size_t>(i)], correction,
                      &trace[static_cast<size_t>(at)]);
          break;
        }
      }
    }
    // Always leave the best policy seen in this tier, never the last update.
    if (!trainer.loadModel(guardDir))
      throw std::runtime_error("failed to restore final protected tier champion");
    trainer.saveTo(cfg.outDir);
    std::error_code guardEc;
    fs::remove_all(guardDir, guardEc);
  }

  // ---- final report ----
  std::printf("\nfinal curriculum evaluation\n");
  int cleared = 0;
  double mean = 0;
  for (int i = 0; i < n; ++i) {
    const State end = trainer.rollout(levels[static_cast<size_t>(i)], nullptr, nullptr);
    const float p = end.won ? 1.0f
                            : std::min(1.0f, end.x / std::max(1.0f, levels[static_cast<size_t>(i)].length));
    mean += p;
    cleared += end.won ? 1 : 0;
    std::printf("  %-26s %6.2f%%%s\n", useable[static_cast<size_t>(i)].name.c_str(),
                p * 100.0f, end.won ? " COMPLETE" : "");
  }
  std::printf("mean %.2f%%, cleared %d/%d\n", mean / std::max(1, n) * 100.0,
              cleared, n);
  return 0;
}

}  // namespace gd

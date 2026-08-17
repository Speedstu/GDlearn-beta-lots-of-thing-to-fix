// SPDX-License-Identifier: MIT
#include "rl/curriculum.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "env/obs.hpp"
#include "io/macro.hpp"
#include "nn/net.hpp"
#include "rl/running.hpp"
#include "search/beam.hpp"

namespace fs = std::filesystem;

namespace gd {
namespace {

// Small deterministic PRNG. The project already has one, but the curriculum
// only needs shuffling and it keeps this file self-contained.
struct Lcg {
  uint64_t s;
  explicit Lcg(uint64_t seed) : s(seed * 2862933555777941757ull + 3037000493ull) {}
  uint32_t next() {
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<uint32_t>(s >> 33);
  }
  int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<uint32_t>(n)); }
};

int threadCount(int requested) {
  if (requested > 0) return requested;
  const unsigned hc = std::thread::hardware_concurrency();
  return hc == 0 ? 1 : static_cast<int>(hc);
}

std::string stemOf(const std::string& path) {
  return fs::path(path).stem().string();
}

std::vector<std::string> collectLevelPaths(const std::vector<std::string>& dirs) {
  std::vector<std::string> out;
  for (const std::string& d : dirs) {
    if (d.empty() || !fs::exists(d)) continue;
    if (fs::is_directory(d)) {
      for (const auto& e : fs::directory_iterator(d))
        if (e.path().extension() == ".gdl") out.push_back(e.path().string());
    } else if (fs::path(d).extension() == ".gdl") {
      out.push_back(d);
    }
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

// Difficulty score. Every term is measured, never guessed:
//   * search cost: the beam width the solver needed (log scale, dominant term)
//   * branching:   nodes expanded per frame of solution
//   * density:     objects per block, i.e. how busy the level is
//   * length:      a long level is more chances to die even if each part is easy
// Unsolved levels are pushed to the very end of the curriculum.
float difficultyScore(const RatedLevel& r, int minBeam) {
  if (!r.solved) return 1e6f;
  const float beamTerm =
      std::log2(std::max(1.0f, static_cast<float>(r.beamNeeded) /
                                   static_cast<float>(std::max(1, minBeam))));
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
        // A level only needs as many frames as its own length can take. Even
        // at the slowest speed tier the player covers 251 units per second,
        // so a 26k-unit level is over in ~6300 frames. Budgeting the global
        // 24000-frame ceiling for every level wasted 4x the backtrack memory
        // for nothing, which is what pushed the process over its address
        // space limit. 1.4x + 600 leaves room for slow/ship sections.
        const float perFrame = phys::SPEEDS[0] * phys::DT;
        int frameCap =
            static_cast<int>(lv.length / std::max(1.0f, perFrame) * 1.4f) + 600;
        if (frameCap > cfg.rateMaxFrames) frameCap = cfg.rateMaxFrames;

        // Bound the beam by the memory budget for THIS level's frame cap. A
        // requested width of 6000 on a long level would otherwise try to
        // allocate gigabytes; now it is silently narrowed instead of dying.
        const double bytesPerSlot = 8.0 * 2.5;  // trail slack included
        int beamCap = static_cast<int>(static_cast<double>(cfg.memBudgetMb) *
                                      1024.0 * 1024.0 /
                                      (bytesPerSlot * std::max(1, frameCap)));
        if (beamCap < 200) beamCap = 200;

        if (cfg.announce) {
          std::lock_guard<std::mutex> lock(ioMutex);
          std::printf("  ... rating %-24s %4.0f blocks, %d frames, beam <= %d\n",
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

      if (!r.solved && r.frames == 0 && r.progress <= 0.0f)
        r.error = "dies at spawn (level starts inside geometry)";
      } catch (const std::exception& e) {
        r.error = e.what();
        r.solved = false;
      } catch (...) {
        r.error = "unknown failure";
        r.solved = false;
      }
      r.score = difficultyScore(r, minBeam);
      rated[static_cast<size_t>(i)] = std::move(r);

      const int n = done.fetch_add(1) + 1;
      std::lock_guard<std::mutex> lock(ioMutex);
      const RatedLevel& rr = rated[static_cast<size_t>(i)];
      if (!rr.error.empty()) {
        std::printf("  [%3d/%3d] %-28s ERROR    %s\n", n, total,
                    rr.name.c_str(), rr.error.c_str());
      } else if (rr.solved) {
        std::printf("  [%3d/%3d] %-28s solved   beam %5d  %4.0f blocks  "
                    "score %8.1f%s\n",
                    n, total, rr.name.c_str(), rr.beamNeeded, rr.lengthBlocks,
                    rr.score, cached ? "  (cached macro)" : "");
      } else {
        // A level stuck at the same percentage no matter how wide the beam is
        // has a wall in the simulator, not a search-budget problem.
        std::printf("  [%3d/%3d] %-28s UNSOLVED beam %5d  %4.0f blocks  "
                    "stuck at %6.2f%% after %d frames\n",
                    n, total, rr.name.c_str(), rr.beamNeeded, rr.lengthBlocks,
                    rr.progress * 100.0f, rr.frames);
      }
      std::fflush(stdout);
    }
  };

  // The beam keeps a backtrack trail of beamWidth * frames entries. At width
  // 6000 that is ~1 GB for ONE level, so one level per core is how a machine
  // dies of a silent bad_alloc halfway through the ranking.
  const int maxBeam =
      cfg.rateBeams.empty()
          ? 400
          : *std::max_element(cfg.rateBeams.begin(), cfg.rateBeams.end());
  // Per-level frames are capped by level length in the worker above; a real GD
  // level lands around 7-9k frames, not the 24000 global ceiling.
  const int framesEst = std::min(cfg.rateMaxFrames, 12000);
  // The trail is a vector of per-frame vectors: each row is its own heap
  // allocation with capacity slack, and the candidate buffer lives beside it.
  // The raw beamWidth*frames*8 figure was ~2.5x too optimistic, which is why a
  // run with a "4096 MB budget" still died.
  const double perThreadMb = static_cast<double>(maxBeam) *
                             static_cast<double>(framesEst) * 8.0 * 2.5 /
                             (1024.0 * 1024.0);
  // A 32-bit process cannot address more than ~2 GB whatever the machine has,
  // and Visual Studio generators default to Win32. That ceiling is real.
  const bool is32bit = sizeof(void*) == 4;
  int budget = cfg.memBudgetMb;
  if (is32bit && budget > 1200) budget = 1200;
  int memCap = static_cast<int>(static_cast<double>(budget) /
                                std::max(1.0, perThreadMb));
  if (memCap < 1) memCap = 1;
  const int nThreads =
      std::min(std::min(threadCount(cfg.threads), total), memCap);
  std::printf("rating %d level(s) with the solver: %d thread(s) "
              "(~%.0f MB per thread, budget %d MB%s)\n",
              total, nThreads, perThreadMb, budget,
              is32bit ? ", 32-bit build" : "");
  if (is32bit)
    std::printf("  warning: 32-bit build; address space is the real limit. "
                "Rebuild with: cmake -B build -A x64\n");
  if (perThreadMb > static_cast<double>(budget))
    std::printf("  warning: one level alone needs ~%.0f MB, above the %d MB "
                "budget. Lower --beams or raise --mem-budget-mb.\n",
                perThreadMb, budget);
  std::fflush(stdout);
  std::vector<std::thread> pool;
  for (int t = 1; t < nThreads; ++t) pool.emplace_back(worker);
  worker();
  for (std::thread& t : pool) t.join();

  std::stable_sort(rated.begin(), rated.end(),
                   [](const RatedLevel& a, const RatedLevel& b) {
                     return a.score < b.score;
                   });
  return rated;
}

namespace {

// Supervised trainer shared by every tier. Holds the imitation dataset, the
// network, and the observation normaliser.
class Trainer {
 public:
  Trainer(const CurriculumConfig& cfg, int obsDimension)
      : cfg_(cfg), D_(obsDimension), rng_(cfg.seed) {
    net_.build(D_, {cfg.hidden, cfg.hidden}, 2, cfg.seed);
    norm_.init(D_);
  }

  bool loadFrom(const std::string& dir) {
    return net_.load(dir + "/policy.bin") && norm_.load(dir + "/obs_norm.bin");
  }
  void saveTo(const std::string& dir) const {
    net_.save(dir + "/policy.bin");
    norm_.save(dir + "/obs_norm.bin");
  }

  int samples() const { return static_cast<int>(Y_.size()); }

  // Replays a macro and records (observation -> button) for every frame.
  void addTrajectory(const Level& level, const State* start,
                     const std::vector<uint8_t>& holds) {
    Sim sim(&level);
    if (start) sim.restore(*start);
    std::vector<float> row(static_cast<size_t>(D_));
    for (size_t f = 0; f < holds.size() && sim.alive(); ++f) {
      encodeObs(sim, row.data());
      X_.insert(X_.end(), row.begin(), row.end());
      Y_.push_back(holds[f] ? 1 : 0);
      sim.step(holds[f] != 0);
    }
    capDataset();
  }

  struct FitStats {
    double loss = 0;
    double accuracy = 0;
  };

  FitStats train(int epochs) {
    const int n = samples();
    FitStats out;
    if (n <= 0) return out;

    // Re-fit the normaliser on everything we know, then train on a normalised
    // copy of the dataset.
    norm_.init(D_);
    norm_.observe(X_.data(), n);
    std::vector<float> Xn = X_;
    norm_.apply(Xn.data(), n);

    // ~80% of frames are "do not press". Without class weights the cheapest
    // policy is one that never presses, and that is exactly what the first
    // version of this trainer learned.
    int64_t nHold = 0;
    for (int i = 0; i < n; ++i) nHold += Y_[static_cast<size_t>(i)] ? 1 : 0;
    const int64_t nRel = n - nHold;
    float w[2] = {1.0f, 1.0f};
    if (nHold > 0 && nRel > 0) {
      w[0] = 0.5f * static_cast<float>(n) / static_cast<float>(nRel);
      w[1] = 0.5f * static_cast<float>(n) / static_cast<float>(nHold);
    }

    std::vector<int> idx(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) idx[static_cast<size_t>(i)] = i;

    const int mb = std::max(1, cfg_.minibatch);
    std::vector<float> buf, logits, values, probs, dLogits, dValues;

    for (int ep = 0; ep < epochs; ++ep) {
      // Anneal inside the stage. At a fixed rate this trainer oscillates and
      // never settles (measured: fit bounced 49% -> 90% -> 73% between
      // epochs); decaying to a tenth of the rate fixes it.
      const float lrEp =
          cfg_.lr * (0.1f + 0.9f * (1.0f - static_cast<float>(ep) /
                                               static_cast<float>(std::max(1, epochs))));
      for (int i = n - 1; i > 0; --i)
        std::swap(idx[static_cast<size_t>(i)],
                  idx[static_cast<size_t>(rng_.below(i + 1))]);

      double epochLoss = 0;
      int64_t correct = 0, seen = 0;
      for (int s = 0; s < n; s += mb) {
        const int bs = std::min(mb, n - s);
        buf.resize(static_cast<size_t>(bs) * D_);
        for (int b = 0; b < bs; ++b)
          std::memcpy(buf.data() + static_cast<size_t>(b) * D_,
                      Xn.data() + static_cast<size_t>(idx[static_cast<size_t>(s + b)]) * D_,
                      sizeof(float) * static_cast<size_t>(D_));

        net_.forward(buf.data(), bs, &logits, &values, /*cache=*/true);
        probs.resize(static_cast<size_t>(bs) * 2);
        dLogits.assign(static_cast<size_t>(bs) * 2, 0.0f);
        dValues.assign(static_cast<size_t>(bs), 0.0f);
        for (int b = 0; b < bs; ++b) {
          nn::softmax(logits.data() + b * 2, 2, probs.data() + b * 2);
          const int label = Y_[static_cast<size_t>(idx[static_cast<size_t>(s + b)])];
          const float wl = w[label];
          epochLoss += -static_cast<double>(wl) *
                       std::log(std::max(1e-8f, probs[static_cast<size_t>(b) * 2 + label]));
          const int pred = probs[static_cast<size_t>(b) * 2 + 1] >
                                   probs[static_cast<size_t>(b) * 2 + 0]
                               ? 1
                               : 0;
          correct += (pred == label) ? 1 : 0;
          seen++;
          for (int k = 0; k < 2; ++k)
            dLogits[static_cast<size_t>(b) * 2 + k] =
                wl * (probs[static_cast<size_t>(b) * 2 + k] -
                      (k == label ? 1.0f : 0.0f)) /
                static_cast<float>(bs);
        }
        net_.zeroGrad();
        net_.backward(buf.data(), bs, dLogits.data(), dValues.data());
        net_.clipGradNorm(cfg_.maxGradNorm);
        net_.adamStep(lrEp);
      }
      out.loss = seen ? epochLoss / static_cast<double>(seen) : 0.0;
      out.accuracy =
          seen ? static_cast<double>(correct) / static_cast<double>(seen) : 0.0;
      if (cfg_.verbose && (ep % 20 == 0 || ep == epochs - 1))
        std::printf("        epoch %3d  loss %.4f  fit %.2f%%\n", ep, out.loss,
                    out.accuracy * 100.0);
    }
    return out;
  }

  // Greedy (argmax) rollout. `trace` receives the state before each frame so a
  // failed attempt can be handed back to the solver.
  State rollout(const Level& level, std::vector<State>* trace,
                std::vector<uint8_t>* holds) {
    Sim sim(&level);
    std::vector<float> row(static_cast<size_t>(D_)), logits, values;
    for (int f = 0; f < cfg_.evalMaxFrames && sim.alive(); ++f) {
      if (trace) trace->push_back(sim.state());
      encodeObs(sim, row.data());
      norm_.apply(row.data(), 1);
      net_.forward(row.data(), 1, &logits, &values, /*cache=*/false);
      const bool hold = logits[1] > logits[0];
      if (holds) holds->push_back(hold ? 1 : 0);
      sim.step(hold);
    }
    return sim.state();
  }

 private:
  void capDataset() {
    const int n = samples();
    if (cfg_.maxSamples <= 0 || n <= cfg_.maxSamples) return;
    // Random subsample. Keeping a uniform sample of everything seen beats
    // dropping the oldest data, which is where the easy tiers live.
    const int keep = cfg_.maxSamples;
    std::vector<int> pick(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) pick[static_cast<size_t>(i)] = i;
    for (int i = n - 1; i > 0; --i)
      std::swap(pick[static_cast<size_t>(i)],
                pick[static_cast<size_t>(rng_.below(i + 1))]);
    pick.resize(static_cast<size_t>(keep));
    std::sort(pick.begin(), pick.end());
    std::vector<float> nx(static_cast<size_t>(keep) * D_);
    std::vector<uint8_t> ny(static_cast<size_t>(keep));
    for (int i = 0; i < keep; ++i) {
      std::memcpy(nx.data() + static_cast<size_t>(i) * D_,
                  X_.data() + static_cast<size_t>(pick[static_cast<size_t>(i)]) * D_,
                  sizeof(float) * static_cast<size_t>(D_));
      ny[static_cast<size_t>(i)] = Y_[static_cast<size_t>(pick[static_cast<size_t>(i)])];
    }
    X_.swap(nx);
    Y_.swap(ny);
  }

  CurriculumConfig cfg_;
  int D_;
  Lcg rng_;
  nn::Net net_;
  RunningNorm norm_;
  std::vector<float> X_;
  std::vector<uint8_t> Y_;
};

}  // namespace

int runCurriculum(const CurriculumConfig& cfg) {
  fs::create_directories(cfg.outDir);

  const auto t0 = std::chrono::steady_clock::now();
  std::vector<RatedLevel> rated = rateLibrary(cfg);
  if (rated.empty()) {
    std::printf("no levels found (looked in %zu director%s)\n",
                cfg.levelDirs.size(), cfg.levelDirs.size() == 1 ? "y" : "ies");
    return 2;
  }

  // ---- the curriculum manifest, easiest first ----
  {
    std::ofstream f(cfg.outDir + "/curriculum.csv");
    f << "rank,name,score,solved,progress,beam_needed,frames,expanded,"
         "length_blocks,objects_per_block,error,path\n";
    for (size_t i = 0; i < rated.size(); ++i) {
      const RatedLevel& r = rated[i];
      f << i << ',' << r.name << ',' << r.score << ',' << (r.solved ? 1 : 0)
        << ',' << r.progress << ',' << r.beamNeeded << ',' << r.frames << ','
        << r.expanded << ',' << r.lengthBlocks << ',' << r.objectsPerBlock
        << ',' << r.error << ',' << r.path << '\n';
    }
  }
  int solvedCount = 0;
  for (const RatedLevel& r : rated) solvedCount += r.solved ? 1 : 0;
  std::printf("\ncurriculum: %zu levels, %d beatable by the solver -> %s\n",
              rated.size(), solvedCount,
              (cfg.outDir + "/curriculum.csv").c_str());
  if (cfg.rateOnly) return 0;

  // Levels the solver cannot beat have no oracle, so there is nothing to
  // imitate. They are kept in the manifest but skipped in training unless
  // asked for explicitly.
  std::vector<RatedLevel> useable;
  for (RatedLevel& r : rated)
    if (r.solved || cfg.keepUnsolved) useable.push_back(std::move(r));
  if (useable.empty()) {
    std::printf("the solver could not beat a single level; nothing to imitate\n");
    return 1;
  }

  // Levels must live somewhere stable: Sim keeps a pointer to them.
  std::vector<Level> levels;
  levels.reserve(useable.size());
  for (const RatedLevel& r : useable)
    levels.push_back(r.path.empty() ? makeTrainingLevel(0, 1)
                                    : Level::loadGdl(r.path));

  Trainer trainer(cfg, obsDim());
  if (cfg.resume && trainer.loadFrom(cfg.outDir))
    std::printf("resumed policy from %s\n", cfg.outDir.c_str());

  std::ofstream log(cfg.outDir + "/curriculum_log.csv");
  log << "tier,round,active_levels,samples,loss,fit,mean_progress,cleared\n";

  const int tiers = std::max(1, cfg.tiers);
  const int n = static_cast<int>(useable.size());
  int active = 0;   // number of levels unlocked so far

  for (int tier = 0; tier < tiers; ++tier) {
    const int upTo = std::min(n, static_cast<int>(std::llround(
                                    static_cast<double>(n) * (tier + 1) / tiers)));
    if (upTo <= active) continue;

    // Unlock this tier: its oracle solutions enter the training mix, while
    // every earlier tier stays in it (that is what prevents forgetting).
    for (int i = active; i < upTo; ++i)
      if (!useable[static_cast<size_t>(i)].oracle.empty())
        trainer.addTrajectory(levels[static_cast<size_t>(i)], nullptr,
                              useable[static_cast<size_t>(i)].oracle);
    active = upTo;

    std::printf("\n=== tier %d/%d: %d level(s) unlocked, hardest so far '%s' ===\n",
                tier + 1, tiers, active,
                useable[static_cast<size_t>(active - 1)].name.c_str());

    for (int round = 0; round < cfg.roundsPerTier; ++round) {
      Trainer::FitStats fit = trainer.train(cfg.epochs);

      // Evaluate the policy alone on everything unlocked.
      double progSum = 0;
      int cleared = 0, worst = 0;
      float worstProg = 2.0f;
      std::vector<State> worstTrace;
      for (int i = 0; i < active; ++i) {
        std::vector<State> trace;
        const State end = trainer.rollout(levels[static_cast<size_t>(i)], &trace,
                                          nullptr);
        const float prog =
            end.won ? 1.0f
                    : std::min(1.0f, end.x / std::max(1.0f,
                                                      levels[static_cast<size_t>(i)].length));
        progSum += prog;
        if (end.won) cleared++;
        if (!end.won && prog < worstProg) {
          worstProg = prog;
          worst = i;
          worstTrace = std::move(trace);
        }
      }
      const double meanProg = progSum / std::max(1, active);
      std::printf("  round %2d  samples %7d  loss %.4f  fit %6.2f%%  "
                  "mean %6.2f%%  cleared %d/%d\n",
                  round, trainer.samples(), fit.loss, fit.accuracy * 100.0,
                  meanProg * 100.0, cleared, active);
      std::fflush(stdout);
      log << tier << ',' << round << ',' << active << ',' << trainer.samples()
          << ',' << fit.loss << ',' << fit.accuracy << ',' << meanProg << ','
          << cleared << '\n';
      log.flush();

      if (cleared >= static_cast<int>(std::ceil(cfg.promote * active))) {
        std::printf("  promotion threshold reached, unlocking the next tier\n");
        break;
      }
      if (worstTrace.empty()) break;

      // ---- DAgger: ask the solver to rescue the level we are worst at ----
      const Level& lv = levels[static_cast<size_t>(worst)];
      const int have = static_cast<int>(worstTrace.size());
      bool rescued = false;
      for (int back : {30, 90, 240, 600}) {
        const int at = have - back;
        if (at <= 0) continue;
        SolveOptions o;
        o.beamWidth = cfg.rescueBeam;
        o.maxFrames = cfg.evalMaxFrames;
        o.stallFrames = 60 * 10;
        o.verbose = false;
        o.hasStart = true;
        o.start = worstTrace[static_cast<size_t>(at)];
        SolveResult r = beamSolve(lv, o);
        if (r.solved) {
          trainer.addTrajectory(
              lv, &worstTrace[static_cast<size_t>(at)],
              minimizeMacro(lv, r.holds, &worstTrace[static_cast<size_t>(at)]));
          rescued = true;
          break;
        }
      }
      std::printf("    %s on '%s' (policy reached %.2f%%)\n",
                  rescued ? "added a corrective demonstration"
                          : "no rescue found; re-training on existing data",
                  useable[static_cast<size_t>(worst)].name.c_str(),
                  worstProg * 100.0f);
    }

    trainer.saveTo(cfg.outDir);
  }

  // ---- final report ----
  std::printf("\nfinal policy evaluation over the whole library\n");
  double progSum = 0;
  int cleared = 0;
  for (int i = 0; i < n; ++i) {
    std::vector<uint8_t> holds;
    const State end = trainer.rollout(levels[static_cast<size_t>(i)], nullptr,
                                      &holds);
    const float prog =
        end.won ? 1.0f
                : std::min(1.0f, end.x / std::max(1.0f,
                                                  levels[static_cast<size_t>(i)].length));
    progSum += prog;
    cleared += end.won ? 1 : 0;
    std::printf("  %-28s %6.2f%%%s\n", useable[static_cast<size_t>(i)].name.c_str(),
                prog * 100.0f, end.won ? "  COMPLETE" : "");
    if (end.won && !cfg.macroDir.empty()) {
      Macro m;
      m.level = useable[static_cast<size_t>(i)].name;
      m.progress = 1.0f;
      m.holds = holds;
      m.save(cfg.macroDir + "/" + useable[static_cast<size_t>(i)].name +
             ".policy.macro");
    }
  }
  trainer.saveTo(cfg.outDir);
  const double secs =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("\npolicy alone: mean %.2f%%, cleared %d/%d   (%.1f min)\n"
              "solver macros for every beatable level are in %s/\n"
              "checkpoint -> %s\n",
              progSum / std::max(1, n) * 100.0, cleared, n, secs / 60.0,
              cfg.macroDir.c_str(), cfg.outDir.c_str());
  return cleared == n ? 0 : 3;
}

}  // namespace gd

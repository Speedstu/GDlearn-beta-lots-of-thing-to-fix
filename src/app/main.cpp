// SPDX-License-Identifier: MIT
// gdlearn2 CLI.
//
//   gdlearn selftest                     physics + net + macro sanity checks
//   gdlearn bench                        simulator throughput
//   gdlearn gen  --out levels/           write the procedural curriculum
//   gdlearn solve  <level.gdl> [opts]    exact beam search -> macro
//   gdlearn train  [opts]                PPO on a level pool
//   gdlearn eval   <run> <level.gdl>     run a checkpoint, write a macro
//   gdlearn replay <level.gdl> <macro>   verify a macro in the simulator
//   gdlearn curriculum [opts]            rate a whole level library by measured
//                                        difficulty, then train easiest-first
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "core/level.hpp"
#include "core/sim.hpp"
#include "env/env.hpp"
#include "env/obs.hpp"
#include "io/macro.hpp"
#include "nn/net.hpp"
#include "rl/curriculum.hpp"
#include "rl/ppo.hpp"
#include "rl/policy_runner.hpp"
#include "search/beam.hpp"

namespace fs = std::filesystem;
using namespace gd;

namespace {

struct Args {
  std::vector<std::string> pos;
  bool has(const std::string& k) const { return kv.count(k) > 0; }
  std::string str(const std::string& k, const std::string& d = "") const {
    auto it = kv.find(k);
    return it == kv.end() ? d : it->second;
  }
  int num(const std::string& k, int d) const {
    auto it = kv.find(k);
    return it == kv.end() ? d : std::atoi(it->second.c_str());
  }
  float real(const std::string& k, float d) const {
    auto it = kv.find(k);
    return it == kv.end() ? d : static_cast<float>(std::atof(it->second.c_str()));
  }
  std::map<std::string, std::string> kv;
};

Args parseArgs(int argc, char** argv, int from) {
  Args a;
  for (int i = from; i < argc; ++i) {
    std::string s = argv[i];
    if (s.rfind("--", 0) == 0) {
      const std::string key = s.substr(2);
      if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
        a.kv[key] = argv[++i];
      } else {
        a.kv[key] = "1";
      }
    } else {
      a.pos.push_back(s);
    }
  }
  return a;
}

std::vector<Level> buildPool(const Args& a) {
  std::vector<Level> pool;
  const std::string dir = a.str("levels", "");
  if (!dir.empty() && fs::exists(dir)) {
    std::vector<std::string> files;
    if (fs::is_directory(dir)) {
      for (const auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".gdl") files.push_back(e.path().string());
      std::sort(files.begin(), files.end());
    } else {
      files.push_back(dir);
    }
    for (const std::string& f : files) pool.push_back(Level::loadGdl(f));
    std::printf("loaded %zu level(s) from %s\n", pool.size(), dir.c_str());
    return pool;
  }
  const int maxDiff = a.num("difficulty", 4);
  const int perDiff = a.num("per-difficulty", 4);
  for (int d = 0; d <= maxDiff; ++d)
    for (int s = 0; s < perDiff; ++s)
      pool.push_back(makeTrainingLevel(d, static_cast<uint64_t>(s) + 1));
  std::printf("generated %zu procedural level(s) (difficulty 0..%d)\n",
              pool.size(), maxDiff);
  return pool;
}

int cmdSelftest() {
  int failures = 0;
  auto check = [&](bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) failures++;
  };
  std::printf("gdlearn2 selftest\n");

  // 1. Flat level: doing nothing must survive and finish.
  Level flat = makeFlatLevel(60);
  {
    Sim sim(&flat);
    while (sim.step(false)) {}
    check(sim.state().won && !sim.state().dead, "idle run finishes a flat level");
  }
  // 2. A jump must leave the ground and come back.
  {
    Sim sim(&flat);
    sim.step(true);
    check(!sim.state().onGround && sim.state().vy > 0, "press produces a jump");
    int frames = 1;
    while (!sim.state().onGround && frames < phys::ticks(2.0f)) { sim.step(false); frames++; }
    check(sim.state().onGround, "cube lands again");
    std::printf("       airtime = %d frames\n", frames);
  }
  // Regression: the dynamic flight corridor ceiling is ONLY for flight
  // modes. A gravity-flipped cube used to be snapped from y~650 to y=600,
  // which let beam search travel through official levels below/through geometry.
  {
    Sim sim(&flat);
    State st = sim.state();
    st.mode = Mode::Cube;
    st.flip = true;
    st.y = 650.0f;
    st.vy = 0.0f;
    st.onGround = false;
    sim.restore(st);
    sim.step(false);
    check(sim.state().y > 620.0f,
          "flipped cube is never clamped by flight corridor ceiling");
  }

  // Regression: an inverted flight mode must never be teleported below
  // the level by a remote "ceiling" reference. The old flipped-only clamp
  // turned (solid.bottom - FLIGHT_CEILING) into a target centre y, which is
  // negative in this exact setup.
  {
    Level lv;
    lv.name = "flipped-ship-regression";
    lv.floorY = 0.0f;
    lv.length = 1000.0f;
    Object ceilingPiece;
    ceilingPiece.kind = Kind::Solid;
    ceilingPiece.x = 1.5f;
    ceilingPiece.y = 200.0f;
    ceilingPiece.hw = 15.0f;
    ceilingPiece.hh = 15.0f;
    lv.add(ceilingPiece);
    lv.finalize();

    Sim sim(&lv);
    State st = sim.state();
    st.mode = Mode::Ship;
    st.flip = true;
    st.onGround = false;
    st.y = 100.0f;
    st.vy = 0.0f;
    sim.restore(st);
    sim.step(false);
    check(!sim.state().dead && sim.state().y >= lv.floorY + sim.state().halfH() &&
              sim.state().y > 80.0f,
          "inverted ship never teleports below floor from remote ceiling");
  }

  // 3. Determinism: same inputs => bit-identical state.
  {
    Level lv = makeTrainingLevel(3, 7);
    std::vector<uint8_t> inputs;
    Sim a(&lv);
    Rng rng(99);
    for (int i = 0; i < 600; ++i) {
      const bool h = rng.chance(0.2f);
      inputs.push_back(h);
      if (!a.step(h)) break;
    }
    Sim b(&lv);
    for (uint8_t h : inputs)
      if (!b.step(h != 0)) break;
    check(std::memcmp(&a.state(), &b.state(), sizeof(State)) == 0,
          "simulator is deterministic");
  }
  // 4. Snapshot / restore round-trip (required by beam search).
  {
    Level lv = makeTrainingLevel(2, 3);
    Sim sim(&lv);
    for (int i = 0; i < 100; ++i) sim.step(i % 7 == 0);
    const State snap = sim.state();
    for (int i = 0; i < 50; ++i) sim.step(true);
    sim.restore(snap);
    check(std::memcmp(&sim.state(), &snap, sizeof(State)) == 0,
          "state snapshot round-trips");
  }
  // 5. Observation encoder writes the whole vector and stays finite.
  {
    Level lv = makeTrainingLevel(5, 11);
    Sim sim(&lv);
    std::vector<float> obs(obsDim(), -999.0f);
    for (int i = 0; i < 200; ++i) sim.step(i % 11 == 0);
    encodeObs(sim, obs.data());
    bool finite = true, touched = true;
    for (float v : obs) {
      if (!std::isfinite(v)) finite = false;
      if (v == -999.0f) touched = false;
    }
    check(finite && touched, "observation encoder is clean");
    std::printf("       obs dim = %d\n", obsDim());
  }
  // 6. Net forward/backward + save/load round-trip.
  {
    nn::Net net;
    net.build(16, {32, 32}, 2, 42);
    std::vector<float> x(16 * 4, 0.1f), logits, values;
    net.forward(x.data(), 4, &logits, &values, true);
    std::vector<float> dl(8, 0.01f), dv(4, 0.01f);
    net.zeroGrad();
    net.backward(x.data(), 4, dl.data(), dv.data());
    const float norm = net.clipGradNorm(0.5f);
    net.adamStep(1e-3f);
    std::vector<float> l2, v2;
    net.forward(x.data(), 4, &l2, &v2, false);
    check(norm > 0 && std::isfinite(l2[0]), "net trains without exploding");

    const std::string tmp = (fs::temp_directory_path() / "gdl_net.bin").string();
    check(net.save(tmp), "net saves");
    nn::Net net2;
    check(net2.load(tmp), "net loads");
    std::vector<float> l3, v3;
    net2.forward(x.data(), 4, &l3, &v3, false);
    check(std::fabs(l3[0] - l2[0]) < 1e-6f, "loaded net matches saved net");
  }
  // 7. Macro round-trip.
  {
    Macro m;
    m.level = "unit";
    m.holds = {0, 0, 1, 1, 1, 0, 1};
    const std::string tmp = (fs::temp_directory_path() / "gdl.macro").string();
    Macro back;
    check(m.save(tmp) && Macro::load(tmp, &back) && back.holds == m.holds,
          "macro round-trips");
  }
  // 8. Beam search must solve a mid-difficulty procedural level.
  {
    Level lv = makeTrainingLevel(3, 5);
    SolveOptions o;
    o.beamWidth = 300;
    o.verbose = false;
    o.maxFrames = phys::ticks(60.0f);
    SolveResult r = beamSolve(lv, o);
    VerifyResult v = verifyMacro(lv, r.holds);
    std::printf("       beam: solved=%d progress=%.1f%% verify=%.1f%%\n",
                static_cast<int>(r.solved), r.progress * 100.0f,
                v.progress * 100.0f);
    check(r.progress > 0.25f, "beam search makes real progress");
    check(std::fabs(v.progress - r.progress) < 0.02f || r.solved,
          "macro replay matches the search");
  }
  std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n",
              failures);
  return failures == 0 ? 0 : 1;
}

int cmdBench(const Args& a) {
  Level lv = makeTrainingLevel(a.num("difficulty", 5), 1);
  std::printf("level '%s': %d objects, %.0f units (%.0f blocks)\n",
              lv.name.c_str(), lv.objectCount(), lv.length,
              lv.length / phys::BLOCK);

  const int64_t target = a.num("steps", 20'000'000);
  Sim sim(&lv);
  Rng rng(7);
  int64_t steps = 0, deaths = 0;
  const auto t0 = std::chrono::steady_clock::now();
  while (steps < target) {
    if (!sim.step(rng.chance(0.15f))) {
      sim.reset();
      deaths++;
    }
    steps++;
  }
  const double sec = std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - t0).count();
  std::printf("sim: %.2fM steps/s single-thread (%.0fx realtime), %lld deaths\n",
              steps / sec / 1e6, steps / sec / static_cast<double>(phys::TPS),
              static_cast<long long>(deaths));

  // Observation encoding is usually the real bottleneck, so measure it too.
  std::vector<float> obs(obsDim());
  const auto t1 = std::chrono::steady_clock::now();
  const int64_t n = 200000;
  for (int64_t i = 0; i < n; ++i) {
    sim.step(rng.chance(0.15f));
    if (!sim.alive()) sim.reset();
    encodeObs(sim, obs.data());
  }
  const double sec1 = std::chrono::duration<double>(
                          std::chrono::steady_clock::now() - t1).count();
  std::printf("obs: %.2fk encodes/s\n", n / sec1 / 1e3);
  return 0;
}

int cmdGen(const Args& a) {
  const std::string out = a.str("out", "levels");
  fs::create_directories(out);
  const int maxDiff = a.num("difficulty", 9);
  const int per = a.num("per-difficulty", 3);
  // Verified generation: a random level is worthless as a curriculum if it is
  // physically impossible. The beam search is fast enough to *prove* every
  // generated level is clearable before it is written to disk.
  const bool verify = !a.has("no-verify");
  const float minProgress = a.real("min-progress", 1.0f);
  const int tries = a.num("tries", 40);
  int n = 0, rejected = 0;
  for (int d = 0; d <= maxDiff; ++d) {
    for (int s = 0; s < per; ++s) {
      Level lv;
      float best = 0;
      bool ok = false;
      uint64_t seed = static_cast<uint64_t>(s) * 1000ull + 1ull;
      for (int t = 0; t < (verify ? tries : 1); ++t, ++seed) {
        lv = makeTrainingLevel(d, seed);
        if (!verify) { ok = true; break; }
        SolveOptions o;
        o.beamWidth = a.num("beam", 600);
        o.stallFrames = phys::ticks(4.0f);
        SolveResult r = beamSolve(lv, o);
        best = std::max(best, r.progress);
        if (r.solved || r.progress >= minProgress) { ok = true; break; }
        rejected++;
      }
      char name[256];
      std::snprintf(name, sizeof(name), "%s/d%d_s%d.gdl", out.c_str(), d, s);
      lv.saveGdl(name);
      n++;
      std::printf("  d%d s%d  %-28s %s\n", d, s, lv.name.c_str(),
                  ok ? "verified clearable"
                     : "WARNING: not proven clearable");
    }
  }
  std::printf("wrote %d levels to %s (%d candidates rejected as unfair)\n", n,
              out.c_str(), rejected);
  return 0;
}

int cmdSolve(const Args& a) {
  if (a.pos.empty()) {
    std::printf("usage: gdlearn solve <level.gdl> [--beam N] [--policy run-dir] "
                "[--prior-weight X] [--out macro]\n");
    return 2;
  }
  Level lv = Level::loadGdl(a.pos[0]);
  std::printf("solving '%s' (%d objects, %.0f blocks)\n", lv.name.c_str(),
              lv.objectCount(), lv.length / phys::BLOCK);

  SolveOptions o;
  o.beamWidth = a.num("beam", 800);
  o.maxFrames = a.num("max-frames", phys::ticks(180.0f));
  o.stallFrames = a.num("stall", phys::ticks(8.0f));
  o.verbose = true;

  PolicyRunner guide;
  if (a.has("policy")) {
    std::string err;
    const std::string dir = a.str("policy");
    if (!guide.load(dir, &err)) {
      std::printf("cannot load policy prior from %s: %s\n", dir.c_str(), err.c_str());
      return 2;
    }
    o.priorWeight = a.real("prior-weight", 8.0f);
    o.priorBatch = [&guide, &lv](const std::vector<State>& states,
                                 std::vector<float>* pHold) {
      guide.probabilities(lv, states, pHold);
    };
    std::printf("guided by %s (%s), prior weight %.2f\n", dir.c_str(),
                guide.schemaMagic().c_str(), o.priorWeight);
  }

  const auto t0 = std::chrono::steady_clock::now();
  SolveResult r = beamSolve(lv, o);
  const double sec = std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - t0).count();

  // Automatic widening: cheap first pass, brute force only where needed.
  int widen = a.num("widen", 2);
  while (!r.solved && widen-- > 0) {
    o.beamWidth *= 4;
    std::printf("not solved (%.2f%%); retrying with beam %d\n",
                r.progress * 100.0f, o.beamWidth);
    SolveResult r2 = beamSolve(lv, o);
    if (r2.progress > r.progress) r = r2;
  }

  VerifyResult v = verifyMacro(lv, r.holds);
  std::printf(
      "%s  progress %.2f%%  frames %d  expanded %lldk  %.1fs  verify %.2f%%\n",
      r.solved ? "SOLVED" : "partial", r.progress * 100.0f, r.frames,
      static_cast<long long>(r.expanded / 1000), sec, v.progress * 100.0f);

  const std::string out = a.str("out", a.pos[0] + ".macro");
  Macro m;
  m.level = lv.name;
  m.progress = v.progress;
  m.holds = r.holds;
  if (m.save(out)) std::printf("macro -> %s\n  %s\n", out.c_str(),
                               m.summary().c_str());
  return r.solved ? 0 : 1;
}

int cmdTrain(const Args& a) {
  std::vector<Level> pool = buildPool(a);
  if (pool.empty()) {
    std::printf("no levels\n");
    return 2;
  }
  PpoConfig cfg;
  cfg.numEnvs = a.num("envs", 64);
  cfg.stepsPerEnv = a.num("steps-per-env", 128);
  cfg.epochs = a.num("epochs", 4);
  cfg.minibatch = a.num("minibatch", 2048);
  cfg.lr = a.real("lr", 3e-4f);
  cfg.gamma = a.real("gamma", 0.995f);
  cfg.totalSteps = static_cast<int64_t>(a.real("total-steps", 5e6f));
  cfg.threads = a.num("threads", 0);
  cfg.seed = static_cast<uint64_t>(a.num("seed", 1234));
  cfg.saveEvery = a.num("save-every", 25);
  cfg.outDir = a.str("out", "runs/default");
  const int h = a.num("hidden", 256);
  cfg.hidden = {h, h};
  fs::create_directories(cfg.outDir);

  Ppo ppo(&pool, cfg);
  if (a.has("resume") && ppo.loadCheckpoint(cfg.outDir))
    std::printf("resumed from %s\n", cfg.outDir.c_str());
  ppo.train();

  // Final report: greedy progress on every level in the pool.
  std::printf("\nfinal greedy evaluation\n");
  float sum = 0;
  int wins = 0;
  for (const Level& lv : pool) {
    Ppo::Rollout r = ppo.evaluate(lv);
    sum += r.progress;
    wins += r.won ? 1 : 0;
    std::printf("  %-24s %6.2f%%%s\n", lv.name.c_str(), r.progress * 100.0f,
                r.won ? "  COMPLETE" : "");
  }
  std::printf("mean %.2f%%   completed %d/%zu\n", sum / pool.size() * 100.0f,
              wins, pool.size());
  return 0;
}

// Splits "a,b,c" into its parts, ignoring empties.
std::vector<std::string> splitCsv(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ',') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

int cmdCurriculum(const Args& a) {
  CurriculumConfig cfg;
  cfg.levelDirs = splitCsv(a.str("levels", "levels_real,levels"));
  cfg.outDir = a.str("out", "runs/curriculum");
  cfg.macroDir = a.str("macros", "macros");

  const std::vector<std::string> beams = splitCsv(a.str("beams", ""));
  if (!beams.empty()) {
    cfg.rateBeams.clear();
    for (const std::string& b : beams) cfg.rateBeams.push_back(std::atoi(b.c_str()));
  }
  cfg.rateMaxFrames = a.num("rate-max-frames", cfg.rateMaxFrames);
  cfg.rateStall = a.num("rate-stall", cfg.rateStall);
  cfg.threads = a.num("threads", 0);
  cfg.memBudgetMb = a.num("mem-budget-mb", cfg.memBudgetMb);
  cfg.announce = !a.has("no-announce");
  cfg.rateOnly = a.has("rate-only");
  cfg.keepUnsolved = a.has("keep-unsolved");
  cfg.reuseMacros = !a.has("no-reuse-macros");

  cfg.genUpTo = a.num("gen-up-to", -1);
  cfg.genPer = a.num("gen-per", 2);

  cfg.tiers = a.num("tiers", 6);
  cfg.roundsPerTier = a.num("rounds", 6);
  cfg.epochs = a.num("epochs", 120);
  cfg.minibatch = a.num("minibatch", 256);
  cfg.lr = a.real("lr", 1e-3f);
  cfg.maxGradNorm = a.real("max-grad-norm", 2.0f);
  cfg.hidden = a.num("hidden", 256);
  cfg.seed = static_cast<uint64_t>(a.num("seed", 7));
  cfg.promote = a.real("promote", 0.75f);
  cfg.maxSamples = a.num("max-samples", 600000);
  cfg.evalMaxFrames = a.num("max-frames", phys::ticks(400.0f));
  cfg.rescueBeam = a.num("rescue-beam", 2500);
  cfg.resume = a.has("resume");
  cfg.verbose = a.has("verbose");

  return runCurriculum(cfg);
}

int cmdEval(const Args& a) {
  if (a.pos.size() < 2) {
    std::printf("usage: gdlearn eval <run-dir> <level.gdl> [--out macro]\n");
    return 2;
  }
  Level lv = Level::loadGdl(a.pos[1]);
  PolicyRunner policy;
  std::string err;
  if (!policy.load(a.pos[0], &err)) {
    std::printf("cannot load checkpoint from %s: %s\n", a.pos[0].c_str(),
                err.c_str());
    return 1;
  }
  PolicyRollout r = policy.evaluate(
      lv, a.num("max-frames", phys::ticks(180.0f)));
  std::printf("%s: %.2f%%%s  schema=%s\n", lv.name.c_str(),
              r.progress * 100.0f, r.won ? "  COMPLETE" : "",
              policy.schemaMagic().c_str());
  Macro m;
  m.level = lv.name;
  m.progress = r.progress;
  m.holds = r.holds;
  const std::string out = a.str("out", a.pos[1] + ".policy.macro");
  if (!m.save(out)) {
    std::printf("cannot save macro -> %s\n", out.c_str());
    return 1;
  }
  std::printf("macro -> %s\n", out.c_str());
  return r.won ? 0 : 3;
}

// ---------------------------------------------------------------- distill ----
// Train the NETWORK to clear a level, using the beam search as an oracle.
//
// Pure PPO on a 900-block real level is hopeless on a laptop: the reward is
// only reachable after ~5000 correct binary decisions in a row. But we already
// have a solver that produces perfect play, so the right algorithm is DAgger:
//   1. imitate the oracle run,
//   2. let the greedy policy play until it dies,
//   3. ask the oracle to solve the level FROM THAT DYING STATE,
//   4. add those corrective labels and retrain.
// Step 3 is what plain behaviour cloning lacks, and it is only possible because
// `State` is a POD the search can be seeded with.
int cmdDistill(const Args& a) {
  if (a.pos.empty()) {
    std::printf("usage: gdlearn distill <level.gdl> [--rounds N] [--beam N] "
                "[--macro f] [--out dir]\n");
    return 2;
  }
  Level lv = Level::loadGdl(a.pos[0]);
  const std::string out = a.str("out", "runs/distill");
  fs::create_directories(out);
  const int beam = a.num("beam", 4000);
  const int rounds = a.num("rounds", 12);
  const int epochs = a.num("epochs", 4);
  const int mb = a.num("minibatch", 512);
  const float lr = a.real("lr", 6e-4f);
  const int maxFrames = a.num("max-frames", phys::ticks(400.0f));

  std::printf("distilling '%s' (%d objects, %.0f blocks)\n", lv.name.c_str(),
              lv.objectCount(), lv.length / phys::BLOCK);

  // ---- oracle ----
  std::vector<uint8_t> oracle;
  if (a.has("macro")) {
    Macro m;
    if (Macro::load(a.str("macro"), &m)) oracle = m.holds;
  }
  if (oracle.empty()) {
    SolveOptions o;
    o.beamWidth = beam;
    o.maxFrames = maxFrames;
    o.stallFrames = phys::ticks(15.0f);
    o.verbose = false;
    SolveResult r = beamSolve(lv, o);
    if (!r.solved) {
      std::printf("oracle could not clear the level (%.2f%%)\n",
                  r.progress * 100.0f);
      return 1;
    }
    oracle = r.holds;
  }
  {
    VerifyResult v = verifyMacro(lv, oracle);
    std::printf("oracle: %zu frames, verified %.2f%%%s\n", oracle.size(),
                v.progress * 100.0f, v.solved ? " COMPLETE" : "");
  }

  const int D = obsDim();
  std::vector<float> X;
  std::vector<uint8_t> Y;

  // Replays `holds` from `start` and records (observation -> action) pairs.
  auto collect = [&](const State* start, const std::vector<uint8_t>& holds) {
    Sim sim(&lv);
    if (start) sim.restore(*start);
    std::vector<float> row(D);
    for (size_t f = 0; f < holds.size() && sim.alive(); ++f) {
      encodeObs(sim, row.data());
      X.insert(X.end(), row.begin(), row.end());
      Y.push_back(holds[f] ? 1 : 0);
      sim.step(holds[f] != 0);
    }
  };
  // Beam-search macros contain a lot of MEANINGLESS presses: holding the
  // button in mid-air as a cube changes nothing, so the solver leaves those
  // bits at whatever value it happened to expand. Imitating that is imitating
  // coin flips, and it caps behaviour cloning at ~76% accuracy. So: drop every
  // press that is not strictly needed, walking backwards so the earliest
  // (safest) press of each pair survives. The result is a canonical macro a
  // network can actually fit -- and a cleaner macro for the real game too.
  auto minimize = [&](const State* start, std::vector<uint8_t> holds) {
    auto beats = [&](const std::vector<uint8_t>& h) {
      Sim sim(&lv);
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
  };

  {
    const size_t before = std::count(oracle.begin(), oracle.end(),
                                     static_cast<uint8_t>(1));
    oracle = minimize(nullptr, oracle);
    const size_t after = std::count(oracle.begin(), oracle.end(),
                                    static_cast<uint8_t>(1));
    std::printf("minimised oracle: %zu -> %zu held frames\n", before, after);
  }
  collect(nullptr, oracle);

  nn::Net net;
  net.build(D, {a.num("hidden", 256), a.num("hidden", 256)}, 2,
            static_cast<uint64_t>(a.num("seed", 7)));
  RunningNorm norm;
  uint64_t rngState = 0x243F6A8885A308D3ull;
  auto nextRand = [&rngState]() {
    rngState = rngState * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<uint32_t>(rngState >> 33);
  };

  // Greedy (argmax) rollout of the current policy.
  auto rollout = [&](std::vector<State>* trace, std::vector<uint8_t>* holds) {
    Sim sim(&lv);
    std::vector<float> row(D), logits, values;
    for (int f = 0; f < maxFrames && sim.alive(); ++f) {
      if (trace) trace->push_back(sim.state());
      encodeObs(sim, row.data());
      norm.apply(row.data(), 1);
      net.forward(row.data(), 1, &logits, &values, false);
      const bool hold = logits[1] > logits[0];
      if (holds) holds->push_back(hold ? 1 : 0);
      sim.step(hold);
    }
    return sim.state();
  };

  std::vector<float> buf, logits, values, probs, dLogits, dValues;
  bool cleared = false;
  std::vector<uint8_t> bestHolds;
  float bestProgress = 0;

  for (int round = 0; round < rounds && !cleared; ++round) {
    int n = static_cast<int>(Y.size());
    // --limit exists for one reason: an overfit sanity check. If the trainer
    // cannot memorise a couple hundred frames, the bug is in the trainer, not
    // in the data.
    if (a.has("limit")) n = std::min(n, a.num("limit", n));
    if (a.has("verbose")) {
      // How much of the data is simply not decidable from the observation?
      // Identical observations with opposite labels are an upper bound on the
      // accuracy any policy of this input space can ever reach.
      std::map<std::string, int> seenRow;
      int conflicts = 0, dups = 0;
      for (int i = 0; i < n; ++i) {
        std::string key(reinterpret_cast<const char*>(X.data() + static_cast<size_t>(i) * D),
                       sizeof(float) * D);
        auto it = seenRow.find(key);
        if (it == seenRow.end()) {
          seenRow.emplace(std::move(key), Y[i]);
        } else {
          dups++;
          if (it->second != Y[i]) conflicts++;
        }
      }
      std::printf("      %d duplicate observations, %d of them with "
                  "contradictory labels (%.1f%% of data)\n",
                  dups, conflicts, 100.0 * conflicts / std::max(1, n));
    }

    // Renormalise on the whole dataset, then train on a normalised copy.
    norm.init(D);
    norm.observe(X.data(), n);
    std::vector<float> Xn = X;
    norm.apply(Xn.data(), n);

    std::vector<int> idx(n);
    for (int i = 0; i < n; ++i) idx[i] = i;

    // Class weighting. ~80% of frames are "release", so an unweighted loss is
    // minimised by a policy that never presses -- which is exactly what the
    // first version learned (it died on the first spike, every single round).
    int64_t nHold = 0;
    for (int i = 0; i < n; ++i) nHold += Y[i] ? 1 : 0;
    const int64_t nRel = n - nHold;
    float w[2] = {1.0f, 1.0f};
    if (nHold > 0 && nRel > 0) {
      w[0] = 0.5f * static_cast<float>(n) / static_cast<float>(nRel);
      w[1] = 0.5f * static_cast<float>(n) / static_cast<float>(nHold);
    }

    double lastLoss = 0, lastAcc = 0;
    for (int ep = 0; ep < epochs; ++ep) {
      for (int i = n - 1; i > 0; --i) std::swap(idx[i], idx[nextRand() % (i + 1)]);
      double epochLoss = 0, gradNormSum = 0;
      int64_t correct = 0, seen = 0, gradNormCount = 0;
      for (int s = 0; s < n; s += mb) {
        const int bs = std::min(mb, n - s);
        double lossSum = 0;
        buf.resize(static_cast<size_t>(bs) * D);
        for (int b = 0; b < bs; ++b)
          std::memcpy(buf.data() + static_cast<size_t>(b) * D,
                      Xn.data() + static_cast<size_t>(idx[s + b]) * D,
                      sizeof(float) * D);

        net.forward(buf.data(), bs, &logits, &values, true);
        probs.resize(static_cast<size_t>(bs) * 2);
        dLogits.assign(static_cast<size_t>(bs) * 2, 0.0f);
        dValues.assign(bs, 0.0f);
        for (int b = 0; b < bs; ++b) {
          nn::softmax(logits.data() + b * 2, 2, probs.data() + b * 2);
          const int label = Y[idx[s + b]];
          const float wl = w[label];
          lossSum += -wl * std::log(std::max(1e-8f, probs[b * 2 + label]));
          const int pred = probs[b * 2 + 1] > probs[b * 2 + 0] ? 1 : 0;
          correct += (pred == label) ? 1 : 0;
          seen++;
          for (int k = 0; k < 2; ++k)
            dLogits[b * 2 + k] =
                wl * (probs[b * 2 + k] - (k == label ? 1.0f : 0.0f)) / bs;
        }
        net.zeroGrad();
        net.backward(buf.data(), bs, dLogits.data(), dValues.data());
        gradNormSum += net.clipGradNorm(a.real("max-grad-norm", 2.0f));
        gradNormCount++;
        net.adamStep(lr);
        epochLoss += lossSum;
      }
      lastLoss = seen ? epochLoss / static_cast<double>(seen) : 0.0;
      lastAcc = seen ? static_cast<double>(correct) / static_cast<double>(seen) : 0.0;
      if (a.has("verbose"))
        std::printf("      epoch %2d  loss %.4f  fit %.2f%%  |grad| %.3e\n", ep,
                    lastLoss, lastAcc * 100.0,
                    gradNormCount ? gradNormSum / gradNormCount : 0.0);
    }

    if (a.has("verbose")) {
      // Sanity: are the inputs finite, and does the policy ever say "press"?
      int64_t bad = 0;
      float lo = 1e30f, hi = -1e30f;
      for (float v : Xn) {
        if (!std::isfinite(v)) bad++;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
      int64_t predHold = 0;
      for (int s = 0; s < n; s += mb) {
        const int bs = std::min(mb, n - s);
        net.forward(Xn.data() + static_cast<size_t>(s) * D, bs, &logits, &values,
                    false);
        for (int b = 0; b < bs; ++b)
          predHold += logits[b * 2 + 1] > logits[b * 2 + 0] ? 1 : 0;
      }
      std::printf("      inputs: %lld non-finite, range [%.2f, %.2f]; "
                  "predicted press on %lld/%d frames (labels %lld)\n",
                  static_cast<long long>(bad), lo, hi,
                  static_cast<long long>(predHold), n,
                  static_cast<long long>(nHold));
    }

    // ---- how far does the policy get on its own? ----
    std::vector<State> trace;
    std::vector<uint8_t> holds;
    const State end = rollout(&trace, &holds);
    const float prog = end.won ? 1.0f : std::min(1.0f, end.x / lv.length);
    if (prog > bestProgress) {
      bestProgress = prog;
      bestHolds = holds;
    }
    std::printf("round %2d  samples %7d  loss %.4f  fit %6.2f%%  greedy %6.2f%%%s\n",
                round, n, lastLoss, lastAcc * 100.0, prog * 100.0f,
                end.won ? "  COMPLETE" : "");
    std::fflush(stdout);
    if (end.won) {
      cleared = true;
      bestHolds = holds;
      bestProgress = 1.0f;
      break;
    }

    // ---- DAgger: ask the oracle to rescue the states we actually visit ----
    const int have = static_cast<int>(trace.size());
    int added = 0;
    for (int back : {phys::ticks(0.5f), phys::ticks(1.5f), phys::ticks(3.333333f), phys::ticks(7.0f)}) {
      const int at = have - back;
      if (at <= 0) continue;
      SolveOptions o;
      o.beamWidth = beam;
      o.maxFrames = maxFrames;
      o.stallFrames = phys::ticks(10.0f);
      o.verbose = false;
      o.hasStart = true;
      o.start = trace[at];
      SolveResult r = beamSolve(lv, o);
      if (r.solved) {
        collect(&trace[at], minimize(&trace[at], r.holds));
        added++;
        break;  // one good rescue per round is enough and keeps rounds fast
      }
    }
    // A little extra coverage from random points of the visited trajectory,
    // so the policy does not overfit the single oracle line.
    if (have > phys::ticks(2.0f)) {
      const int at = static_cast<int>(nextRand() % static_cast<uint32_t>(have - phys::ticks(1.0f)));
      SolveOptions o;
      o.beamWidth = std::max(600, beam / 4);
      o.maxFrames = maxFrames;
      o.stallFrames = phys::ticks(6.0f);
      o.verbose = false;
      o.hasStart = true;
      o.start = trace[at];
      SolveResult r = beamSolve(lv, o);
      if (r.solved) {
        collect(&trace[at], minimize(&trace[at], r.holds));
        added++;
      }
    }
    if (added == 0)
      std::printf("    (no rescue found this round; keeping existing data)\n");
  }

  if (!net.save(out + "/policy.bin") ||
      !norm.save(out + "/obs_norm.bin") ||
      !writePolicySchema(out, "gdlearn_imitation_v2", D)) {
    std::printf("failed to save native distill checkpoint -> %s\n", out.c_str());
    return 1;
  }
  Macro m;
  m.level = lv.name;
  m.progress = bestProgress;
  m.holds = bestHolds;
  const std::string macroPath = out + "/policy.macro";
  m.save(macroPath);
  VerifyResult v = verifyMacro(lv, bestHolds);
  std::printf("\npolicy best %.2f%% (verified %.2f%%%s)\ncheckpoint -> %s\n",
              bestProgress * 100.0f, v.progress * 100.0f,
              v.solved ? " COMPLETE" : "", out.c_str());
  std::printf("macro      -> %s\n", macroPath.c_str());
  return cleared ? 0 : 3;
}

// Frame-by-frame state dump: the debugging tool the old project never had.
int cmdTrace(const Args& a) {
  if (a.pos.empty()) {
    std::printf("usage: gdlearn trace <level.gdl> [--macro f] [--from N] [--jumps 10,20]\n");
    return 2;
  }
  Level lv = Level::loadGdl(a.pos[0]);
  std::vector<uint8_t> holds;
  if (a.has("macro")) {
    Macro m;
    if (Macro::load(a.str("macro"), &m)) holds = m.holds;
  }
  std::vector<int> jumpFrames;
  if (a.has("jumps")) {
    std::string s = a.str("jumps");
    size_t p = 0;
    while (p < s.size()) {
      size_t c = s.find(',', p);
      jumpFrames.push_back(std::atoi(s.substr(p, c - p).c_str()));
      if (c == std::string::npos) break;
      p = c + 1;
    }
  }
  const int total = a.num("frames", 240);
  const int from = a.num("from", 0);
  const int holdLen = a.num("hold", 1);
  Sim sim(&lv);
  std::printf("frame  hold      x(bl)   y(bl)     vy  ground dead mode flip pad orb contact deathuid\n");
  for (int f = 0; f < total; ++f) {
    bool hold = f < static_cast<int>(holds.size()) ? holds[f] != 0 : false;
    for (int j : jumpFrames)
      if (f >= j && f < j + holdLen) hold = true;
    const bool aliveBefore = sim.alive();
    sim.step(hold);
    const State& s = sim.state();
    if (f >= from)
      std::printf("%5d  %s   %8.3f %7.3f %6.2f     %d    %d    %d    %d %d %d %d %d\n", f,
                  hold ? "HOLD" : "....", s.x / phys::BLOCK,
                  s.y / phys::BLOCK, s.vy, s.onGround ? 1 : 0,
                  s.dead ? 1 : 0, static_cast<int>(s.mode), s.flip ? 1 : 0,
                  s.lastPadUid, s.lastOrbUid, sim.lastContactUid(), sim.deathUid());
    if (!aliveBefore) break;
  }
  return 0;
}

int cmdReplay(const Args& a) {
  if (a.pos.size() < 2) {
    std::printf("usage: gdlearn replay <level.gdl> <file.macro>\n");
    return 2;
  }
  Level lv = Level::loadGdl(a.pos[0]);
  Macro m;
  if (!Macro::load(a.pos[1], &m)) {
    std::printf("cannot read macro\n");
    return 1;
  }
  VerifyResult v = verifyMacro(lv, m.holds);
  std::printf("%s: %.2f%% over %d frames (%.1fs) %s\n", lv.name.c_str(),
              v.progress * 100.0f, v.frames, v.frames / static_cast<float>(phys::TPS),
              v.solved ? "COMPLETE" : "");
  return v.solved ? 0 : 1;
}

// ---------------------------------------------------------------- render ---
// Self-contained HTML viewer: level geometry + the exact simulated trajectory,
// frame by frame. No server, no dependency, no build step -- one file you
// double-click. Reading a table of floats to work out why the bot dies is
// hopeless; watching the cube hit the spike takes one second.
const char* kViewerTemplate = R"HTML(<!doctype html>
<meta charset="utf-8"><title>gdlearn viewer</title>
<style>
body{margin:0;background:#12141c;color:#dfe3ee;font:13px/1.5 system-ui,sans-serif}
header{display:flex;gap:10px;align-items:center;padding:8px 12px;background:#191c26;
  border-bottom:1px solid #2a2f3d;flex-wrap:wrap}
canvas{display:block;background:#0d0f16}
button,select{background:#2b3040;color:#dfe3ee;border:1px solid #3a4157;border-radius:6px;
  padding:5px 10px;cursor:pointer}
button:hover{background:#394054}
input[type=range]{flex:1;min-width:180px}
.stat{font-variant-numeric:tabular-nums;color:#9aa3bb}
.stat b{color:#fff}
.dead{color:#ff6b6b}.win{color:#7cf5a0}
.legend{padding:4px 12px;background:#151824;color:#6f7891;font-size:12px}
.legend i{display:inline-block;width:9px;height:9px;margin:0 4px 0 12px;border-radius:2px}
</style>
<header>
 <button id=play>play</button>
 <button id=death>go to end</button>
 <input type=range id=scrub min=0 value=0>
 <select id=speed>
  <option value=0.15>0.15x</option><option value=0.5>0.5x</option>
  <option value=1 selected>1x</option><option value=2>2x</option>
 </select>
 <span class=stat>frame <b id=ff>0</b>/<span id=tf></span></span>
 <span class=stat>x <b id=xx></b></span>
 <span class=stat>progress <b id=pp></b></span>
 <span class=stat id=ss></span>
</header>
<div class=legend>
 <i style="background:#8892b0"></i>solid <i style="background:#ff4d5a"></i>hazard
 <i style="background:#ffd93d"></i>pad <i style="background:#4dd2ff"></i>orb
 <i style="background:#c77dff"></i>portal <i style="background:#5ef08a"></i>speed
 &nbsp;&nbsp;space = play/pause, arrows = step frame
</div>
<canvas id=c></canvas>
<script>
const D = __GD_DATA__;
const objs = D.objects, tr = D.trajectory;
const c = document.getElementById('c'), g = c.getContext('2d');
const MODES = ['cube','ship','ball','ufo','wave','robot','spider','swing'];
const COL = ['#8892b0','#ff4d5a','#ffd93d','#4dd2ff','#c77dff','#5ef08a'];
let i = 0, playing = false, rate = 1, S = 0.55, acc = 0, last = 0;
function resize(){
  const d = devicePixelRatio || 1;
  c.width = innerWidth * d; c.height = (innerHeight - 78) * d;
  c.style.width = innerWidth + 'px'; c.style.height = (innerHeight - 78) + 'px';
  g.setTransform(d, 0, 0, d, 0, 0); draw();
}
addEventListener('resize', resize);
function draw(){
  const W = c.width / (devicePixelRatio || 1), H = c.height / (devicePixelRatio || 1);
  const st = tr[Math.min(i, tr.length - 1)] || [0, 15, 0, 0, 0, 0];
  const camX = st[0] - W * 0.32 / S, baseY = H - 70;
  g.fillStyle = '#0d0f16'; g.fillRect(0, 0, W, H);
  g.strokeStyle = '#39405a'; g.lineWidth = 1;
  g.beginPath(); g.moveTo(0, baseY + 0.5); g.lineTo(W, baseY + 0.5); g.stroke();
  if (st[6]){
    const cy = baseY - st[6] * S;
    g.strokeStyle = '#6d5a8f'; g.setLineDash([7, 7]);
    g.beginPath(); g.moveTo(0, cy); g.lineTo(W, cy); g.stroke();
    g.setLineDash([]);
    g.fillStyle = '#6d5a8f'; g.font = '11px system-ui';
    g.fillText('flight ceiling', 8, cy - 5);
  }
  const x0 = camX - 60, x1 = camX + W / S + 60;
  for (let n = 0; n < objs.length; n++){
    const o = objs[n];
    if (o[2] < x0 || o[2] > x1) continue;
    const sx = (o[2] - camX) * S, sy = baseY - o[3] * S, hw = o[4] * S, hh = o[5] * S;
    g.fillStyle = COL[o[0]] || '#888';
    if (o[0] === 1){
      g.beginPath(); g.moveTo(sx - hw, sy + hh); g.lineTo(sx, sy - hh);
      g.lineTo(sx + hw, sy + hh); g.closePath(); g.fill();
    } else if (o[0] === 3){
      g.beginPath(); g.arc(sx, sy, Math.max(hw, 3), 0, 6.2832); g.fill();
    } else {
      g.fillRect(sx - hw, sy - hh, hw * 2, hh * 2);
    }
  }
  const trail = 90;
  g.strokeStyle = '#3f7d63'; g.beginPath();
  for (let f = Math.max(0, i - trail); f <= i && f < tr.length; f++){
    const p = tr[f], px = (p[0] - camX) * S, py = baseY - p[1] * S;
    if (f === Math.max(0, i - trail)) g.moveTo(px, py); else g.lineTo(px, py);
  }
  g.stroke();
  const half = (st[4] & 1) ? 9 : 15, sx = (st[0] - camX) * S, sy = baseY - st[1] * S;
  g.save(); g.translate(sx, sy); g.rotate(st[2] * Math.PI / 180);
  g.fillStyle = st[5] === 1 ? '#ff4d5a' : '#7cf5a0';
  g.fillRect(-half * S, -half * S, half * 2 * S, half * 2 * S);
  g.fillStyle = '#0d0f16';
  g.fillRect(-half * S * 0.35, -half * S * 0.35, half * S * 0.7, half * S * 0.7);
  g.restore();
  if (st[4] & 4){
    g.strokeStyle = '#ffd93d'; g.lineWidth = 2;
    g.strokeRect(sx - half * S - 3, sy - half * S - 3, half * 2 * S + 6, half * 2 * S + 6);
  }
  document.getElementById('ff').textContent = i;
  document.getElementById('xx').textContent = (st[0] / 30).toFixed(1) + ' bl';
  document.getElementById('pp').textContent = (100 * st[0] / D.length).toFixed(2) + '%';
  const s = document.getElementById('ss');
  s.className = 'stat ' + (st[5] === 1 ? 'dead' : st[5] === 2 ? 'win' : '');
  s.textContent = MODES[st[3]] + (st[4] & 1 ? ' mini' : '') + (st[4] & 2 ? ' flipped' : '') +
    (st[5] === 1 ? '  DEAD' : st[5] === 2 ? '  COMPLETE' : '');
}
function tick(t){
  if (playing){
    acc += (t - last) * rate;
    while (acc > 16.667){ acc -= 16.667; if (i < tr.length - 1) i++; else playing = false; }
    document.getElementById('scrub').value = i; draw();
  }
  last = t; requestAnimationFrame(tick);
}
document.getElementById('play').onclick = function(){
  playing = !playing; this.textContent = playing ? 'pause' : 'play';
  if (i >= tr.length - 1) i = 0;
};
document.getElementById('death').onclick = function(){
  i = tr.length - 1; document.getElementById('scrub').value = i; draw();
};
document.getElementById('scrub').oninput = function(){ i = +this.value; draw(); };
document.getElementById('speed').onchange = function(){ rate = +this.value; };
addEventListener('keydown', function(e){
  if (e.code === 'Space'){ e.preventDefault(); document.getElementById('play').click(); }
  if (e.code === 'ArrowRight'){ i = Math.min(tr.length - 1, i + 1); }
  if (e.code === 'ArrowLeft'){ i = Math.max(0, i - 1); }
  if (e.code === 'ArrowRight' || e.code === 'ArrowLeft'){
    document.getElementById('scrub').value = i; draw();
  }
});
document.getElementById('scrub').max = tr.length - 1;
document.getElementById('tf').textContent = tr.length - 1;
resize();
requestAnimationFrame(tick);
</script>
)HTML";

int cmdRender(const Args& a) {
  if (a.pos.empty()) {
    std::printf("usage: gdlearn render <level.gdl> [macro] [--out f.html] [--solve] [--beam N]\n");
    return 2;
  }
  Level lv = Level::loadGdl(a.pos[0]);
  std::vector<uint8_t> holds;
  std::string src = "no input (idle run)";
  if (a.pos.size() > 1) {
    Macro m;
    if (!Macro::load(a.pos[1], &m)) {
      std::printf("cannot read macro %s\n", a.pos[1].c_str());
      return 1;
    }
    holds = m.holds;
    src = a.pos[1];
  } else if (a.has("solve")) {
    SolveOptions so;
    so.beamWidth = a.num("beam", 1500);
    so.maxFrames = a.num("max-frames", phys::ticks(400.0f));
    SolveResult r = beamSolve(lv, so);
    holds = r.holds;
    src = r.solved ? "beam search (solved)" : "beam search (best effort)";
  }
  // Replay the macro through the real simulator: what you watch is exactly
  // what the physics did, never a separate re-implementation that can drift.
  Sim sim(&lv);
  std::string traj = "[";
  const int maxFrames = a.num("max-frames", phys::ticks(400.0f));
  char buf[192];
  for (int f = 0; f < maxFrames; ++f) {
    const bool hold = f < static_cast<int>(holds.size()) ? holds[f] != 0 : false;
    sim.step(hold);
    const State& s = sim.state();
    const int flags = (s.mini ? 1 : 0) | (s.flip ? 2 : 0) | (hold ? 4 : 0);
    const int status = s.dead ? 1 : (s.won ? 2 : 0);
    std::snprintf(buf, sizeof buf, "%s[%.1f,%.1f,%.0f,%d,%d,%d,%.0f]", f ? "," : "",
                  s.x, s.y, s.rotation, static_cast<int>(s.mode), flags, status,
                  sim.ceiling());
    traj += buf;
    if (s.dead || s.won) break;
  }
  traj += "]";

  std::string objsJson = "[";
  bool first = true;
  for (const Object& o : lv.objects()) {
    std::snprintf(buf, sizeof buf, "%s[%d,%d,%.1f,%.1f,%.1f,%.1f]",
                  first ? "" : ",", static_cast<int>(o.kind), o.sub, o.x, o.y,
                  o.hw, o.hh);
    objsJson += buf;
    first = false;
  }
  objsJson += "]";

  std::string data = "{\"length\":";
  std::snprintf(buf, sizeof buf, "%.1f", lv.length);
  data += buf;
  data += ",\"objects\":" + objsJson + ",\"trajectory\":" + traj + "}";

  std::string html = kViewerTemplate;
  const size_t at = html.find("__GD_DATA__");
  html.replace(at, 11, data);

  std::string out = a.str("out", "");
  if (out.empty()) {
    fs::path p(a.pos[0]);
    out = p.stem().string() + ".html";
  }
  std::FILE* fp = std::fopen(out.c_str(), "wb");
  if (!fp) {
    std::printf("cannot write %s\n", out.c_str());
    return 1;
  }
  std::fwrite(html.data(), 1, html.size(), fp);
  std::fclose(fp);

  const State& s = sim.state();
  std::printf("%s -> %s\n  %d objects, %.2f%% reached, %s\n  input: %s\n",
              a.pos[0].c_str(), out.c_str(), lv.objectCount(),
              100.0f * sim.progress(), s.won ? "COMPLETE" : (s.dead ? "died" : "ran out of frames"),
              src.c_str());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf(
        "gdlearn2 - Geometry Dash bot toolkit\n\n"
        "  selftest                       physics/net/search sanity checks\n"
        "  bench    [--steps N]           simulator throughput\n"
        "  gen      [--out dir]           write procedural curriculum levels\n"
        "  solve    <level.gdl>           exact beam search -> frame macro\n"
        "  train    [--levels dir]        PPO training\n"
        "  eval     <run> <level.gdl>     run a checkpoint\n"
        "  replay   <level.gdl> <macro>   verify a macro\n"
        "  render   <level.gdl> [macro]   self-contained HTML viewer\n");
    return 1;
  }
  const std::string cmd = argv[1];
  const Args a = parseArgs(argc, argv, 2);
  try {
    if (cmd == "selftest") return cmdSelftest();
    if (cmd == "bench") return cmdBench(a);
    if (cmd == "gen") return cmdGen(a);
    if (cmd == "solve") return cmdSolve(a);
    if (cmd == "train") return cmdTrain(a);
    if (cmd == "eval") return cmdEval(a);
    if (cmd == "replay") return cmdReplay(a);
    if (cmd == "render") return cmdRender(a);
    if (cmd == "trace") return cmdTrace(a);
    if (cmd == "distill") return cmdDistill(a);
    if (cmd == "curriculum") return cmdCurriculum(a);
  } catch (const std::exception& e) {
    std::printf("error: %s\n", e.what());
    return 1;
  }
  std::printf("unknown command: %s\n", cmd.c_str());
  return 1;
}

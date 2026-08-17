// SPDX-License-Identifier: MIT
// Curriculum training: learn every level in a library, easiest first.
//
// Why a curriculum at all? A real Geometry Dash level is a chain of thousands
// of frame-perfect binary decisions. Reward only exists at the end, so plain
// RL on "Stereo Madness" -- let alone on an extreme demon -- is a needle in a
// 2^5000 haystack. Two ideas fix that:
//
//   1. ORDER THE LIBRARY BY MEASURED DIFFICULTY. Not by the name the level
//      author picked, and not by a hand-written guess: we run the beam search
//      on each level and record how much search it actually needed. A level
//      that falls to a width-400 beam is genuinely easy; one that needs 6000
//      is genuinely hard. That number is the curriculum order.
//   2. LEARN FROM AN ORACLE, NOT FROM LUCK. For every level the solver can
//      beat, its (minimised) winning macro becomes supervised data, and where
//      the policy dies we re-run the solver FROM THE STATE OF DEATH to get the
//      corrective label (DAgger). The network never has to discover a 5000
//      frame solution by chance.
//
// Tiers unlock one at a time, and every earlier tier stays in the training mix
// so the policy does not forget the basics while learning the hard stuff.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/level.hpp"
#include "core/sim.hpp"

namespace gd {

struct CurriculumConfig {
  // Level sources. Directories are scanned for *.gdl (non-recursively).
  std::vector<std::string> levelDirs;
  std::string outDir = "runs/curriculum";
  std::string macroDir = "macros";

  // ---- difficulty rating -------------------------------------------------
  // Escalating beam widths. The first width that beats the level is the
  // level's "search cost", the backbone of the difficulty score.
  std::vector<int> rateBeams = {400, 1500, 6000};
  int rateMaxFrames = 60 * 400;
  int rateStall = 60 * 12;
  int threads = 0;              // 0 = hardware concurrency
  int memBudgetMb = 2048;       // caps concurrency; the beam is memory-hungry
  bool announce = true;         // print each level before solving it
  bool rateOnly = false;
  bool keepUnsolved = false;    // train on levels the solver cannot beat too
  bool reuseMacros = true;      // load cached macros from macroDir if present

  // ---- optional generated levels ----------------------------------------
  // Procedural levels are appended to the library, which is how you get a
  // smooth ramp *below* and *between* the real levels.
  int genUpTo = -1;             // -1 = none; otherwise difficulty 0..genUpTo
  int genPer = 2;

  // ---- training ----------------------------------------------------------
  int tiers = 6;
  int roundsPerTier = 6;
  int epochs = 120;
  int minibatch = 256;
  float lr = 1e-3f;
  float maxGradNorm = 2.0f;
  int hidden = 256;
  uint64_t seed = 7;
  float promote = 0.75f;        // clear this fraction of a tier to unlock next
  int maxSamples = 600000;      // memory cap on the imitation dataset
  int evalMaxFrames = 60 * 400;
  int rescueBeam = 2500;
  bool resume = false;
  bool verbose = false;
};

struct RatedLevel {
  std::string path;
  std::string name;
  float score = 0;              // difficulty, higher = harder
  bool solved = false;
  float progress = 0;           // best fraction reached (the whole diagnosis)
  std::string error;            // non-empty if rating this level threw
  int beamNeeded = 0;
  int frames = 0;
  int64_t expanded = 0;
  float lengthBlocks = 0;
  float objectsPerBlock = 0;
  std::vector<uint8_t> oracle;  // minimised winning macro ({} if unsolved)
};

// Drops every button press that is not strictly required to still beat the
// level. Beam-search macros are full of presses that change nothing (holding
// the button mid-air as a cube), and imitating those is imitating coin flips.
// Walks backwards so the earliest press of an equivalent pair survives.
// `start` may be null to mean "from the beginning of the level".
std::vector<uint8_t> minimizeMacro(const Level& level,
                                   std::vector<uint8_t> holds,
                                   const State* start = nullptr);

// Rates a whole library. Returns entries sorted easiest -> hardest.
std::vector<RatedLevel> rateLibrary(const CurriculumConfig& cfg);

// Full pipeline: rate, then train tier by tier. Returns a process exit code.
int runCurriculum(const CurriculumConfig& cfg);

}  // namespace gd

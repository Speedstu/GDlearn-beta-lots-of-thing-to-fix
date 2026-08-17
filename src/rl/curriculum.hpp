// SPDX-License-Identifier: MIT
// Curriculum = exact oracle generation + DAgger imitation + strict promotion.
// Levels are ordered by measured search difficulty; every teacher trajectory
// must replay to 100% before it is admitted to the dataset.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/level.hpp"
#include "core/sim.hpp"

namespace gd {

struct CurriculumConfig {
  std::vector<std::string> levelDirs;
  std::string outDir = "runs/curriculum";
  std::string macroDir = "macros";

  std::vector<int> rateBeams = {400, 1500, 6000};
  int rateMaxFrames = phys::ticks(400.0f);
  int rateStall = phys::ticks(12.0f);
  int threads = 0;
  int memBudgetMb = 2048;
  bool announce = true;
  bool rateOnly = false;
  bool keepUnsolved = false;
  bool reuseMacros = true;

  int genUpTo = -1;
  int genPer = 2;

  int tiers = 6;
  int roundsPerTier = 6;
  int epochs = 120;
  int minibatch = 256;
  float lr = 1e-3f;
  float maxGradNorm = 2.0f;
  int hidden = 256;
  uint64_t seed = 7;
  float promote = 0.75f;
  int maxSamples = 600000;
  int evalMaxFrames = phys::ticks(400.0f);
  int rescueBeam = 2500;
  bool resume = false;
  bool verbose = false;
};

struct RatedLevel {
  std::string path;
  std::string name;
  float score = 0;
  bool solved = false;
  float progress = 0;
  std::string error;
  int beamNeeded = 0;
  int frames = 0;
  int64_t expanded = 0;
  float lengthBlocks = 0;
  float objectsPerBlock = 0;
  std::vector<uint8_t> oracle;
};

std::vector<uint8_t> minimizeMacro(const Level& level,
                                   std::vector<uint8_t> holds,
                                   const State* start = nullptr);

std::vector<RatedLevel> rateLibrary(const CurriculumConfig& cfg);
int runCurriculum(const CurriculumConfig& cfg);

}  // namespace gd

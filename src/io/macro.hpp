// SPDX-License-Identifier: MIT
// Frame-perfect input macro: the deliverable of the whole project.
//
// A macro is just "is the button held on frame i", 60 entries per second.
// It is what the solver outputs, what the evaluator verifies, and what the
// Windows player replays into the real game. Text format so it is diffable
// and hand-editable.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gd {

struct Macro {
  std::string level;
  int fps = 60;
  float progress = 0;   // progress reached when it was recorded
  std::vector<uint8_t> holds;

  bool save(const std::string& path) const;
  static bool load(const std::string& path, Macro* out);

  // Compact human view: press/release frame pairs.
  std::string summary(int maxEvents = 40) const;
};

}  // namespace gd

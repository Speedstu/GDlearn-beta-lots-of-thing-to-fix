// SPDX-License-Identifier: MIT
// Native-tick input macro: deterministic solver/trainer interchange format.
//
// A macro is simply "is the button held on physics tick i". Geometry Dash
// 2.2+ vanilla physics runs at 240 TPS, so mixing an old 60-Hz macro with a
// 240-TPS simulator silently changes every timing by 4x.  Macro::load therefore
// rejects non-native tick rates instead of allowing a corrupt oracle into
// DAgger / evaluation.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/physics.hpp"

namespace gd {

struct Macro {
  std::string level;
  int fps = phys::TPS;           // historical field name; semantically TPS
  float progress = 0;
  std::vector<uint8_t> holds;

  bool save(const std::string& path) const;
  static bool load(const std::string& path, Macro* out);

  std::string summary(int maxEvents = 40) const;
};

}  // namespace gd

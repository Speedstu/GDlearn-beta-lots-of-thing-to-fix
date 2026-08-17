// SPDX-License-Identifier: MIT
// Observation encoder.
//
// The old version fed a 20x14 absolute grid + raw pixel-ish values, which made
// the policy relearn the same obstacle at every Y offset. Here everything is
// expressed in GRAVITY-RELATIVE, PLAYER-RELATIVE, BLOCK-NORMALISED units, so a
// spike is the same input whether you are upside-down in a ship or on the
// floor as a cube. That single change is worth more than any extra layer.
#pragma once

#include <vector>

#include "core/sim.hpp"

namespace gd {

struct ObsSpec {
  static constexpr int kCols = 16;   // blocks ahead (1 block per column)
  static constexpr int kRows = 12;   // +-6 blocks around the player
  static constexpr int kChannels = 3;  // solid / hazard / interactive
  static constexpr int kGrid = kCols * kRows * kChannels;
  // 12 kinematic + 8 mode one-hot + 5 speed-tier one-hot + 4 sub-block phase.
  // Kept a little larger than what we write so adding a feature never has to
  // overflow into the grid (the first version wrote 25 values into 24 slots).
  static constexpr int kScalars = 32;
  static constexpr int kDim = kGrid + kScalars;
};

// Writes exactly ObsSpec::kDim floats into `out`.
void encodeObs(const Sim& sim, float* out);

int obsDim();

}  // namespace gd

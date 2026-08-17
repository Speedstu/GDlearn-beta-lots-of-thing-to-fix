// SPDX-License-Identifier: MIT
// Observation encoder.
//
// Geometry is expressed in GRAVITY-RELATIVE, PLAYER-RELATIVE,
// BLOCK-NORMALISED units.  The dense local grid is complemented by a compact
// ordered object stream: a grid tells the policy "something occupies this
// cell", while the tokens retain the exact distance/size/type information
// needed for frame-perfect demon timing.
#pragma once

#include "core/sim.hpp"

namespace gd {

struct ObsSpec {
  static constexpr int kCols = 16;     // blocks ahead (1 block / column)
  static constexpr int kRows = 12;     // +-6 blocks around the player
  static constexpr int kChannels = 3;  // solid / hazard / interactive
  static constexpr int kGrid = kCols * kRows * kChannels;

  // 12 kinematic + 8 mode one-hot + 5 speed-tier one-hot + 4 sub-block phase.
  static constexpr int kScalars = 32;

  // Nearest gameplay objects ahead of / overlapping the player.  Per token:
  //   centre dx, gravity-relative dy, leading-edge dx, half-width, half-height,
  //   six kind one-hots (solid/hazard/pad/orb/portal/speed), sub-kind.
  // This is deliberately small and fixed-size: no recurrent state, no heap
  // allocation and no dependence on absolute level time/identity.
  static constexpr int kObjectTokens = 20;
  static constexpr int kTokenFeatures = 12;
  static constexpr int kTokens = kObjectTokens * kTokenFeatures;

  static constexpr int kDim = kGrid + kScalars + kTokens;  // 848
};

// Writes exactly ObsSpec::kDim floats into `out`.
void encodeObs(const Sim& sim, float* out);

int obsDim();

}  // namespace gd

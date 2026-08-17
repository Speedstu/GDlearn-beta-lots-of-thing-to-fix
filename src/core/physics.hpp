// SPDX-License-Identifier: MIT
// Geometry Dash 2.2+ physics constants. Single source of truth.
//
// Timing model:
//   * vanilla gameplay physics is stepped at 240 TPS
//   * 1 block = 30 world units
//   * horizontal speed is expressed in world units / second
//   * vertical velocity is expressed in world units / 240-Hz tick
//   * vertical acceleration is expressed in world units / tick^2
//
// Most published / reverse-engineered GD vertical constants historically use
// the old 60-Hz-frame convention.  We retain those values below as named
// LEGACY_* constants and convert them exactly:
//   velocity_240 = velocity_60 / 4
//   accel_240    = accel_60 / 16
// This preserves real-time trajectories while allowing an input decision at
// every native 240-Hz physics step (including Click-on-Steps precision).
#pragma once

#include <array>

namespace gd::phys {

constexpr float BLOCK = 30.0f;
constexpr int TPS = 240;
constexpr float FPS = static_cast<float>(TPS);  // compatibility alias
constexpr float DT = 1.0f / static_cast<float>(TPS);
constexpr float LEGACY_FPS = 60.0f;
constexpr float TICKS_PER_LEGACY_FRAME = static_cast<float>(TPS) / LEGACY_FPS;
constexpr float VEL_60_TO_TICK = 1.0f / TICKS_PER_LEGACY_FRAME;          // 1/4
constexpr float ACCEL_60_TO_TICK2 = VEL_60_TO_TICK * VEL_60_TO_TICK;    // 1/16

// ---------------------------------------------------------------- speeds ----
// world units / second. Indexed by speed tier.
constexpr std::array<float, 5> SPEEDS = {
    251.16f,  // slow
    311.58f,  // normal
    387.42f,  // fast
    468.00f,  // faster
    576.00f,  // fastest
};

// ------------------------------------------------------------------ cube ----
constexpr float CUBE_GRAVITY = 0.958199f * ACCEL_60_TO_TICK2;
constexpr float CUBE_JUMP = 11.180000f * VEL_60_TO_TICK;
constexpr float CUBE_TERMINAL = 15.0f * VEL_60_TO_TICK;
constexpr float CUBE_ROT_PER_FRAME = 6.0f * VEL_60_TO_TICK;  // deg / 240-Hz tick

// ------------------------------------------------------------------ ship ----
constexpr float SHIP_GRAVITY = 0.42f * ACCEL_60_TO_TICK2;
constexpr float SHIP_THRUST = 0.84f * ACCEL_60_TO_TICK2;
constexpr float SHIP_MAX_VY = 8.0f * VEL_60_TO_TICK;

// ------------------------------------------------------------------ ball ----
constexpr float BALL_GRAVITY = 0.70f * ACCEL_60_TO_TICK2;
constexpr float BALL_TERMINAL = 11.0f * VEL_60_TO_TICK;

// ------------------------------------------------------------------- ufo ----
constexpr float UFO_GRAVITY = 0.72f * ACCEL_60_TO_TICK2;
constexpr float UFO_JUMP = 7.20f * VEL_60_TO_TICK;
constexpr float UFO_TERMINAL = 13.0f * VEL_60_TO_TICK;

// ------------------------------------------------------------------ wave ----
// Wave velocity is assigned directly from horizontal units/tick in sim.cpp.
constexpr float WAVE_SLOPE = 1.0f;
constexpr float WAVE_SLOPE_MINI = 2.0f;

// ----------------------------------------------------------------- robot ----
constexpr float ROBOT_GRAVITY = 0.958199f * ACCEL_60_TO_TICK2;
constexpr float ROBOT_JUMP = 9.60f * VEL_60_TO_TICK;
constexpr float ROBOT_HOLD_BOOST = 0.62f * ACCEL_60_TO_TICK2;
// Historical 15 x 60-Hz frames = 0.25 s = 60 native ticks.
constexpr int ROBOT_MAX_HOLD_FRAMES = 60;
constexpr float ROBOT_TERMINAL = 15.0f * VEL_60_TO_TICK;

// ---------------------------------------------------------------- spider ----
constexpr float SPIDER_GRAVITY = 0.958199f * ACCEL_60_TO_TICK2;
constexpr float SPIDER_TERMINAL = 15.0f * VEL_60_TO_TICK;

// ----------------------------------------------------------------- swing ----
constexpr float SWING_GRAVITY = 0.62f * ACCEL_60_TO_TICK2;
constexpr float SWING_MAX_VY = 9.0f * VEL_60_TO_TICK;

// ------------------------------------------------------------- boosts ------
constexpr float PAD_YELLOW = 16.10f * VEL_60_TO_TICK;
constexpr float PAD_PINK = 10.70f * VEL_60_TO_TICK;
constexpr float PAD_RED = 21.50f * VEL_60_TO_TICK;
constexpr float PAD_BLUE = 12.00f * VEL_60_TO_TICK;

constexpr float ORB_YELLOW = 11.18f * VEL_60_TO_TICK;
constexpr float ORB_PINK = 8.20f * VEL_60_TO_TICK;
constexpr float ORB_RED = 15.60f * VEL_60_TO_TICK;
constexpr float ORB_BLUE = 11.18f * VEL_60_TO_TICK;
constexpr float ORB_GREEN = 11.18f * VEL_60_TO_TICK;
constexpr float ORB_BLACK = -16.10f * VEL_60_TO_TICK;
constexpr float ORB_DASH_SPEED = 9.0f * VEL_60_TO_TICK;

// ------------------------------------------------------------------ world ---
constexpr float GROUND_Y = 0.0f;
constexpr float CEILING_Y = 2400.0f;
constexpr float FLIGHT_CEILING = 300.0f;
constexpr float KILL_BELOW = -900.0f;

constexpr float HITBOX_CUBE = 15.0f;
constexpr float HITBOX_LETHAL_SCALE = 0.30f;
constexpr float MINI_SCALE = 0.60f;

// Six real-time minutes at native 240 TPS.
constexpr int MAX_FRAMES = TPS * 60 * 6;

// Convert real-time seconds to native physics ticks without sprinkling magic
// 60/240 constants through search / training code.
constexpr int ticks(float seconds) {
  return static_cast<int>(seconds * static_cast<float>(TPS) + 0.5f);
}

}  // namespace gd::phys

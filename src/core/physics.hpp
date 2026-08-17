// SPDX-License-Identifier: MIT
// GD physics constants. Single source of truth.
//
// Unit convention (IMPORTANT, this is what the old codebase got wrong):
//   * 1 block            = 30 units
//   * horizontal speed   = units / SECOND   (matches GD's internal xVel)
//   * vertical velocity  = units / FRAME    (matches GD's internal yVel)
//   * 1 frame            = 1/60 s (GD's physics tick; 240Hz substeps are a
//                          rendering detail, gameplay is decided at 60Hz)
// Mixing those two conventions is the #1 source of "my sim doesn't match the
// game" bugs, so every constant below is tagged with its unit.
#pragma once

#include <array>

namespace gd::phys {

constexpr float BLOCK = 30.0f;          // units per block
constexpr float FPS = 60.0f;            // physics ticks per second
constexpr float DT = 1.0f / FPS;        // seconds per tick

// ---------------------------------------------------------------- speeds ----
// units/second. Indexed by SpeedTier.
constexpr std::array<float, 5> SPEEDS = {
    251.16f,  // 0.5x  (slow)
    311.58f,  // 1.0x  (normal)
    387.42f,  // 2.0x  (fast)
    468.00f,  // 3.0x  (faster)
    576.00f,  // 4.0x  (fastest)
};

// ------------------------------------------------------------------ cube ----
// units/frame^2 and units/frame.
constexpr float CUBE_GRAVITY = 0.958199f;
constexpr float CUBE_JUMP = 11.180000f;
constexpr float CUBE_TERMINAL = 15.0f;      // |yVel| clamp while falling
constexpr float CUBE_ROT_PER_FRAME = 6.0f;  // degrees, cosmetic only

// ------------------------------------------------------------------ ship ----
constexpr float SHIP_GRAVITY = 0.42f;   // pulled toward gravity dir
constexpr float SHIP_THRUST = 0.84f;    // while holding, against gravity dir
constexpr float SHIP_MAX_VY = 8.0f;

// ------------------------------------------------------------------ ball ----
constexpr float BALL_GRAVITY = 0.70f;
constexpr float BALL_TERMINAL = 11.0f;

// ------------------------------------------------------------------- ufo ----
constexpr float UFO_GRAVITY = 0.72f;
constexpr float UFO_JUMP = 7.20f;       // impulse on each fresh press, midair
constexpr float UFO_TERMINAL = 13.0f;

// ------------------------------------------------------------------ wave ----
// Wave has no acceleration: vy is a direct function of the input.
constexpr float WAVE_SLOPE = 1.0f;      // vy = +-slope * (speed * DT)
constexpr float WAVE_SLOPE_MINI = 2.0f;

// ----------------------------------------------------------------- robot ----
constexpr float ROBOT_GRAVITY = 0.958199f;
constexpr float ROBOT_JUMP = 9.60f;         // initial impulse
constexpr float ROBOT_HOLD_BOOST = 0.62f;   // extra lift per held frame
constexpr int ROBOT_MAX_HOLD_FRAMES = 15;   // variable-height jump window
constexpr float ROBOT_TERMINAL = 15.0f;

// ---------------------------------------------------------------- spider ----
constexpr float SPIDER_GRAVITY = 0.958199f;
constexpr float SPIDER_TERMINAL = 15.0f;

// ----------------------------------------------------------------- swing ----
constexpr float SWING_GRAVITY = 0.62f;
constexpr float SWING_MAX_VY = 9.0f;

// ------------------------------------------------------------- boosts ------
constexpr float PAD_YELLOW = 16.10f;    // units/frame
constexpr float PAD_PINK = 10.70f;
constexpr float PAD_RED = 21.50f;
constexpr float PAD_BLUE = 12.00f;      // also flips gravity

constexpr float ORB_YELLOW = 11.18f;
constexpr float ORB_PINK = 8.20f;
constexpr float ORB_RED = 15.60f;
constexpr float ORB_BLUE = 11.18f;      // flips gravity
constexpr float ORB_GREEN = 11.18f;     // flips gravity, keeps direction
constexpr float ORB_BLACK = -16.10f;    // slams toward gravity dir
constexpr float ORB_DASH_SPEED = 9.0f;

// ------------------------------------------------------------------ world ---
constexpr float GROUND_Y = 0.0f;        // top surface of the floor
constexpr float CEILING_Y = 2400.0f;    // hard kill plane above
// How far a flying player may rise above the surface underneath. GD keeps you
// inside the visible corridor: the camera rests on the ground below, so a ship
// climbs about ten blocks and no further. Without this the solver just flies
// over the whole level.
constexpr float FLIGHT_CEILING = 300.0f;
constexpr float KILL_BELOW = -900.0f;   // hard kill plane below

// Player hitbox half-extents (units). GD uses a slightly inset box for the
// lethal check and a larger one for solid resolution; we model both.
constexpr float HITBOX_CUBE = 15.0f;        // half of 30x30
constexpr float HITBOX_LETHAL_SCALE = 0.30f;  // inner box used vs hazards
constexpr float MINI_SCALE = 0.60f;

// Max frames a single attempt may last before we call it a timeout.
constexpr int MAX_FRAMES = 60 * 60 * 6;  // 6 minutes

}  // namespace gd::phys

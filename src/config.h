#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <vector>
#include <cstdint>

// ============================================================================
// Geometry Dash Memory Addresses (GD 2.206 Windows)
// Use Cheat Engine to verify/update for your GD version.
// Pointer chain: base_module + BASE_OFFSET -> ... -> player data
// ============================================================================

namespace gd {

constexpr const char* PROCESS_NAME = "GeometryDash.exe";

// GameManager -> PlayLayer -> Player pointer chain
constexpr uintptr_t BASE_OFFSET              = 0x3222D0;
constexpr uintptr_t PLAYLAYER_OFFSET         = 0x164;
constexpr uintptr_t PLAYER1_OFFSET           = 0x224;

// Player data (offsets from player object)
constexpr uintptr_t OFF_POS_X                = 0x67C;
constexpr uintptr_t OFF_POS_Y                = 0x680;
constexpr uintptr_t OFF_SPEED                = 0x648;
constexpr uintptr_t OFF_Y_ACCEL              = 0x760;
constexpr uintptr_t OFF_ROTATION             = 0x020;
constexpr uintptr_t OFF_IS_DEAD              = 0x63F;
constexpr uintptr_t OFF_ON_GROUND            = 0x73E;
constexpr uintptr_t OFF_GRAVITY_FLIPPED      = 0x63E;
constexpr uintptr_t OFF_GAMEMODE             = 0x638;
constexpr uintptr_t OFF_PLAYER_SIZE          = 0x644;
constexpr uintptr_t OFF_IS_HOLDING           = 0x611;
constexpr uintptr_t OFF_HAS_RING             = 0x612;

// PlayLayer data (offsets from PlayLayer)
constexpr uintptr_t OFF_LEVEL_LENGTH         = 0x3B4;
constexpr uintptr_t OFF_ATTEMPT              = 0x4A8;
constexpr uintptr_t OFF_IS_PRACTICE          = 0x495;
constexpr uintptr_t OFF_PERCENT              = 0x3C0;
constexpr uintptr_t OFF_IS_PLAYING           = 0x2EC;
constexpr uintptr_t OFF_TIME                 = 0x2EC;

} // namespace gd

// ============================================================================
// PPO Hyperparameters
// ============================================================================
namespace ppo_config {

// Network architecture (PPO)
// Start with 128→128 for fast learning, scale up later for superhuman
constexpr int ACTION_SIZE       = 2;     // [no_click, click]
constexpr int HIDDEN_SIZE_1     = 128;   // First hidden layer
constexpr int HIDDEN_SIZE_2     = 128;   // Second hidden layer
constexpr int HIDDEN_SIZE_3     = 64;    // Third hidden layer
// Once the bot learns basics, retrain with 256→256→128 for superhuman

// Training
constexpr float LEARNING_RATE   = 3e-4f;     // Standard PPO learning rate
constexpr float GAMMA           = 0.99f;    // Discount factor
constexpr float GAE_LAMBDA      = 0.95f;    // GAE lambda
constexpr float CLIP_EPSILON    = 0.2f;     // PPO clipping
constexpr float ENTROPY_COEF    = 0.02f;    // KL-from-uniform regularization (never vanishes)
constexpr float VALUE_COEF      = 0.5f;     // Value loss weight
constexpr float MAX_GRAD_NORM   = 0.5f;     // Gradient clipping

// Batching
constexpr int STEPS_PER_UPDATE  = 4096;     // Bigger rollout for better gradient estimates
constexpr int MINI_BATCH_SIZE   = 256;
constexpr int PPO_EPOCHS        = 4;        // Fewer epochs = less overfitting per update

// Schedule
constexpr int TOTAL_TIMESTEPS   = 50000000;  // 50M steps for superhuman
constexpr int SAVE_INTERVAL     = 50000;
constexpr int LOG_INTERVAL      = 1000;

} // namespace ppo_config

// ============================================================================
// Environment Config
// ============================================================================
namespace env_config {

constexpr int TICK_RATE             = 60;      // Actions per second
constexpr int MAX_EPISODE_STEPS     = 30000;
constexpr float DEATH_PENALTY       = -1.0f;
constexpr float PROGRESS_SCALE      = 1.0f;
constexpr float SURVIVAL_REWARD     = 0.01f;
constexpr float COMPLETION_BONUS    = 10.0f;
constexpr float SPEED_REWARD_SCALE  = 0.1f;

} // namespace env_config

// ============================================================================
// Reward Weights (GigaLearn style)
// ============================================================================
namespace reward_weights {

constexpr float PROGRESS        = 5.0f;
constexpr float SURVIVAL        = 0.5f;
constexpr float DEATH           = 3.0f;
constexpr float COMPLETION      = 10.0f;
constexpr float SPEED           = 0.5f;
constexpr float SMOOTH_FLIGHT   = 0.2f;
constexpr float ALTITUDE        = 0.1f;

} // namespace reward_weights

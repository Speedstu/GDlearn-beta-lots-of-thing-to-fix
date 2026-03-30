#include "environment.h"
#include <cmath>
#include <algorithm>
#include <iostream>

// ============================================================================
// Grid scan constants
// The bot sees a grid of cells around itself — like the neuron grid from
// the video, but fed into a neural net instead of hand-crafted rules.
// ============================================================================
static constexpr int GRID_COLS = 12;     // 12 columns ahead (~360 units)
static constexpr int GRID_ROWS = 8;      // 8 rows (4 above, 4 below)
static constexpr float CELL_SIZE = 30.0f; // 1 GD block = 30 units
// Grid obs size: GRID_COLS * GRID_ROWS * 2 channels (solid + hazard)
static constexpr int GRID_OBS_SIZE = GRID_COLS * GRID_ROWS * 2;  // = 192
// Player state features
static constexpr int PLAYER_OBS_SIZE = 14;
// Total observation size
static constexpr int TOTAL_OBS_SIZE = PLAYER_OBS_SIZE + GRID_OBS_SIZE;  // = 206

Environment::Environment() {
    obsSize_ = TOTAL_OBS_SIZE;
    obsBuf_.resize(obsSize_, 0.0f);
}

void Environment::loadLevel(const LevelData& level) {
    sim_.loadLevel(level);
}

void Environment::setRewards(RewardManager&& rewards) {
    rewards_ = std::move(rewards);
}

GameState Environment::simToGameState(const SimPlayer& p, bool playing) const {
    GameState s;
    s.playerX = p.x;
    s.playerY = p.y;
    s.playerSpeed = p.speed;
    s.playerYAccel = p.yVelocity;
    s.playerRotation = p.rotation;
    s.playerSize = p.size;
    s.isDead = p.isDead;
    s.isOnGround = p.onGround;
    s.gravityFlipped = p.gravityFlipped;
    s.isHolding = p.isHolding;
    s.gameMode = p.gameMode;
    s.levelLength = sim_.getLevel().totalLength;
    s.isPlaying = playing;
    s.percent = sim_.getProgressPercent();
    s.isAirborne = !p.onGround;
    s.justJumped = (!p.onGround && prevState_.isOnGround); // was on ground, now airborne

    // Find nearest hazard ahead (within 10 blocks = 300 units)
    s.nearestHazardDist = 999.0f;
    for (const auto& h : sim_.getLevel().hazards) {
        float dx = h.x - p.x;
        if (dx > 0.0f && dx < 300.0f) {
            s.nearestHazardDist = std::min(s.nearestHazardDist, dx);
        }
    }
    return s;
}

std::vector<float> Environment::reset() {
    sim_.reset();
    rewards_.reset();

    curState_ = simToGameState(sim_.getPlayer(), true);
    prevState_ = curState_;
    episodeCount_++;

    return buildObservation();
}

StepResult Environment::step(int action) {
    prevState_ = curState_;

    bool alive = sim_.step(action);

    curState_ = simToGameState(sim_.getPlayer(), !sim_.getPlayer().isDead);

    bool done = !alive || sim_.isCompleted();
    float reward = rewards_.computeReward(curState_, prevState_, done);

    float progress = sim_.getProgressPercent();
    if (progress > bestProgress_) bestProgress_ = progress;

    StepResult result;
    result.obs = buildObservation();
    result.reward = reward;
    result.done = done;
    result.progress = progress;
    result.steps = sim_.getStepCount();
    result.state = curState_;  // Include game state for rendering/debug

    return result;
}

// ============================================================================
// Build observation vector
// ============================================================================
std::vector<float> Environment::buildObservation() {
    std::vector<float> obs;
    obs.reserve(obsSize_);

    const auto& p = sim_.getPlayer();

    // -- Player state features (normalized) --
    obs.push_back(p.x / std::max(1.0f, sim_.getLevel().totalLength)); // progress [0,1]
    obs.push_back(p.y / 600.0f);                                       // height [0,1]
    obs.push_back(p.yVelocity / 15.0f);                                // y velocity [-1,1]
    obs.push_back(p.speed / physics::SPEED_QUAD);                       // speed [0,1]
    obs.push_back(p.onGround ? 1.0f : 0.0f);
    obs.push_back(p.gravityFlipped ? 1.0f : 0.0f);
    obs.push_back(p.isHolding ? 1.0f : 0.0f);

    // Gamemode one-hot (8 modes)
    for (int i = 0; i < 8; i++) {
        obs.push_back(p.gameMode == i ? 1.0f : 0.0f);
    }

    // Delta Y from last frame (momentum indicator)
    float dy = (p.y - p.prevY) / 30.0f;
    obs.push_back(std::clamp(dy, -1.0f, 1.0f));

    // -- Grid scan (the "eyes" of the bot) --
    scanGrid(p, obs);

    // Pad to expected size if needed
    while ((int)obs.size() < obsSize_) obs.push_back(0.0f);

    return obs;
}

// ============================================================================
// Grid scan: 2D grid centered on player, scans ahead for blocks and spikes
// Channel 0: solid (1.0 = block present, 0.0 = air)
// Channel 1: hazard (1.0 = spike present, 0.0 = safe)
// This is MUCH faster than the video's individual neurons because we use
// the spatial index for O(1) lookups per cell.
// ============================================================================
void Environment::scanGrid(const SimPlayer& player, std::vector<float>& obs) {
    float baseX = player.x;
    float baseY = player.y;

    // Grid: 20 columns ahead, 14 rows centered on player
    // Uses simulator spatial index for O(bucket) queries per cell
    for (int row = 0; row < GRID_ROWS; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            float cellX = baseX + (col - 1) * CELL_SIZE;
            float cellY = baseY + (row - GRID_ROWS / 2) * CELL_SIZE;
            float radius = CELL_SIZE * 0.5f;

            obs.push_back(sim_.hasSolidAt(cellX, cellY, radius) ? 1.0f : 0.0f);
            obs.push_back(sim_.hasHazardAt(cellX, cellY, radius) ? 1.0f : 0.0f);
        }
    }
}

// ============================================================================
// Build observation from live game state (for playLive mode)
// Uses provided position but queries the loaded level for grid scan
// ============================================================================
std::vector<float> Environment::buildObservationFromState(const GameState& state) {
    std::vector<float> obs;
    obs.reserve(obsSize_);

    // -- Player state features (from live state) --
    float levelLen = std::max(1.0f, state.levelLength);
    obs.push_back(state.playerX / levelLen);
    obs.push_back(state.playerY / 600.0f);
    obs.push_back(state.playerYAccel / 15.0f);
    obs.push_back(state.playerSpeed / physics::SPEED_QUAD);
    obs.push_back(state.isOnGround ? 1.0f : 0.0f);
    obs.push_back(state.gravityFlipped ? 1.0f : 0.0f);
    obs.push_back(state.isHolding ? 1.0f : 0.0f);

    // Gamemode one-hot (8 modes)
    for (int i = 0; i < 8; i++) {
        obs.push_back(state.gameMode == i ? 1.0f : 0.0f);
    }

    // Delta Y (can't compute from prev in live, use velocity as proxy)
    float dy = state.playerYAccel / 15.0f;
    obs.push_back(std::clamp(dy, -1.0f, 1.0f));

    // -- Grid scan at live position --
    scanGridAt(state.playerX, state.playerY, obs);

    // Pad to expected size if needed
    while ((int)obs.size() < obsSize_) obs.push_back(0.0f);

    return obs;
}

// ============================================================================
// Scan grid at specific position (for live mode)
// Uses the simulator's level data (spatial index) to query blocks/spikes
// ============================================================================
void Environment::scanGridAt(float x, float y, std::vector<float>& obs) {
    float baseX = x;
    float baseY = y;

    for (int row = 0; row < GRID_ROWS; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            float cellX = baseX + (col - 1) * CELL_SIZE;
            float cellY = baseY + (row - GRID_ROWS / 2) * CELL_SIZE;
            float radius = CELL_SIZE * 0.5f;

            obs.push_back(sim_.hasSolidAt(cellX, cellY, radius) ? 1.0f : 0.0f);
            obs.push_back(sim_.hasHazardAt(cellX, cellY, radius) ? 1.0f : 0.0f);
        }
    }
}

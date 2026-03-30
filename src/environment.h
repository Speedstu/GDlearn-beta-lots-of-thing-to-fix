#pragma once

#include "simulator.h"
#include "rewards.h"
#include <vector>
#include <array>

// ============================================================================
// Environment: wraps Simulator + Rewards into an RL-ready interface
// Like RLGym/GigaLearn's environment but for Geometry Dash.
//
// Observation = vector of floats describing game state
// Action      = 0 (release) or 1 (click)
// Reward      = weighted sum of reward functions
// ============================================================================

struct StepResult {
    std::vector<float> obs;
    float reward;
    bool done;
    float progress;
    int steps;
    GameState state;  // Current game state for rendering/debug
};

class Environment {
public:
    Environment();

    void loadLevel(const LevelData& level);
    void setRewards(RewardManager&& rewards);

    // Reset environment, return initial observation
    std::vector<float> reset();

    // Take one action, return result
    StepResult step(int action);

    int getObsSize() const { return obsSize_; }
    int getActionSize() const { return 2; } // binary: click or don't
    const Simulator& getSim() const { return sim_; }

    // Build observation from live game state (for playLive mode)
    std::vector<float> buildObservationFromState(const GameState& state);

    // Stats
    float getBestProgress() const { return bestProgress_; }
    int getEpisodeCount() const { return episodeCount_; }

private:
    Simulator sim_;
    RewardManager rewards_;
    GameState prevState_;
    GameState curState_;
    int obsSize_ = 0;
    float bestProgress_ = 0.0f;
    int episodeCount_ = 0;

    // Pre-allocated observation buffer (avoids alloc every step)
    std::vector<float> obsBuf_;

    // Build observation vector from simulator state
    std::vector<float> buildObservation();

    // Scan grid at specific position (for live mode)
    void scanGridAt(float x, float y, std::vector<float>& obs);

    // Convert SimPlayer to GameState for reward functions
    GameState simToGameState(const SimPlayer& p, bool playing) const;

    // Scan ahead: what objects are in front of the player
    // This gives the bot "vision" like the neuron grid in the video
    void scanAhead(const SimPlayer& player, std::vector<float>& obs);

    // Scan grid: a 2D grid of what's around the player (blocks/spikes/air)
    void scanGrid(const SimPlayer& player, std::vector<float>& obs);
};

#pragma once

#include "memory_reader.h"
#include <vector>
#include <string>
#include <memory>
#include <cmath>

// ============================================================================
// Base Reward class (like GigaLearn's reward system)
// Each reward function is modular: you can mix, weight, and stack them.
// ============================================================================

class RewardFunction {
public:
    virtual ~RewardFunction() = default;
    virtual float GetReward(const GameState& state, const GameState& prevState, bool isFinal) = 0;
    virtual std::string GetName() const = 0;
    virtual void Reset() {} // called at episode start
};

// ============================================================================
// Concrete Rewards
// ============================================================================

// Reward for making forward progress in the level
class ProgressReward : public RewardFunction {
public:
    float GetReward(const GameState& state, const GameState& prevState, bool isFinal) override {
        float dx = state.playerX - prevState.playerX;
        return std::max(0.0f, dx / 30.0f); // normalize by ~block size
    }
    std::string GetName() const override { return "Progress"; }
};

// Penalty on death
class DeathPenalty : public RewardFunction {
public:
    float GetReward(const GameState& state, const GameState& prevState, bool isFinal) override {
        if (state.isDead && !prevState.isDead) return -1.0f;
        return 0.0f;
    }
    std::string GetName() const override { return "Death"; }
};

// Small reward for staying alive each step
class SurvivalReward : public RewardFunction {
public:
    float GetReward(const GameState& state, const GameState& prevState, bool isFinal) override {
        if (!state.isDead && state.isPlaying) return 0.01f;
        return 0.0f;
    }
    std::string GetName() const override { return "Survival"; }
};

// Big bonus for completing the level
class CompletionReward : public RewardFunction {
public:
    float GetReward(const GameState& state, const GameState& prevState, bool isFinal) override {
        if (isFinal && !state.isDead && state.progressPercent() >= 99.0f) {
            return 10.0f;
        }
        return 0.0f;
    }
    std::string GetName() const override { return "Completion"; }
};

// Reward for maintaining speed (don't slow down)
class SpeedReward : public RewardFunction {
public:
    float GetReward(const GameState& state, const GameState& prevState, bool isFinal) override {
        return std::max(0.0f, state.playerSpeed) * 0.001f;
    }
    std::string GetName() const override { return "Speed"; }
};

// Reward for smooth flight in ship/UFO/wave modes (minimize Y oscillation)
class SmoothFlightReward : public RewardFunction {
public:
    float GetReward(const GameState& state, const GameState& prevState, bool isFinal) override {
        // Only active in flying modes (ship=1, ufo=3, wave=4, swing=7)
        if (state.gameMode == 1 || state.gameMode == 3 ||
            state.gameMode == 4 || state.gameMode == 7) {
            float yChange = std::abs(state.playerY - prevState.playerY);
            // Less Y oscillation = higher reward (capped)
            return std::max(0.0f, 0.02f - yChange * 0.0001f);
        }
        return 0.0f;
    }
    std::string GetName() const override { return "SmoothFlight"; }
};

// Penalize for being too high or too low (avoid going off-screen)
class AltitudeReward : public RewardFunction {
public:
    float GetReward(const GameState& state, const GameState& prevState, bool isFinal) override {
        // Reasonable Y range for GD is roughly 0-600
        if (state.playerY < 50.0f || state.playerY > 550.0f) {
            return -0.01f;
        }
        return 0.0f;
    }
    std::string GetName() const override { return "Altitude"; }
};

// Reward for jumping at the right time (near spikes) / penalize useless jumps
class JumpTimingReward : public RewardFunction {
public:
    float GetReward(const GameState& state, const GameState& prevState, bool isFinal) override {
        // Relevant in click-to-act modes: cube(0), ball(2), robot(5), spider(6)
        if (state.gameMode != 0 && state.gameMode != 2 &&
            state.gameMode != 5 && state.gameMode != 6) return 0.0f;

        float reward = 0.0f;

        if (state.justJumped) {
            // Did we jump near a spike? (within ~5 blocks = 150 units ahead)
            if (state.nearestHazardDist < 150.0f) {
                // Good jump! Reward scales with proximity (closer = better timing)
                reward += 0.1f * (1.0f - state.nearestHazardDist / 150.0f);
            } else {
                // Useless jump — no spike nearby, slight penalty
                reward -= 0.02f;
            }
        }

        return reward;
    }
    std::string GetName() const override { return "JumpTiming"; }
};

// Reward for successfully passing over a hazard (was nearby, now behind us)
class HazardDodgeReward : public RewardFunction {
    float prevHazardDist_ = 999.0f;
public:
    void Reset() override { prevHazardDist_ = 999.0f; }
    float GetReward(const GameState& state, const GameState& prevState, bool isFinal) override {
        float reward = 0.0f;

        // If a hazard was close and now it's behind us (dist increased or gone), we dodged it
        if (prevHazardDist_ < 60.0f && state.nearestHazardDist > prevHazardDist_ + 20.0f) {
            reward += 0.3f; // big reward for each spike cleared
        }

        prevHazardDist_ = state.nearestHazardDist;
        return reward;
    }
    std::string GetName() const override { return "HazardDodge"; }
};

// Reward for percentage milestones (bonus at 25%, 50%, 75%)
class MilestoneReward : public RewardFunction {
    float lastMilestone_ = 0.0f;
public:
    void Reset() override { lastMilestone_ = 0.0f; }
    float GetReward(const GameState& state, const GameState& prevState, bool isFinal) override {
        float pct = state.progressPercent();
        float reward = 0.0f;
        // Check for milestone crossings
        for (float m = 10.0f; m <= 100.0f; m += 10.0f) {
            if (pct >= m && lastMilestone_ < m) {
                reward += 1.0f; // bonus for each 10% milestone
                lastMilestone_ = m;
            }
        }
        return reward;
    }
    std::string GetName() const override { return "Milestone"; }
};

// Reward for surviving near hazards (creates learning gradient)
class ProximityReward : public RewardFunction {
    float prevDist_ = 999.0f;
public:
    void Reset() override { prevDist_ = 999.0f; }
    float GetReward(const GameState& state, const GameState& prevState, bool isFinal) override {
        // Reward for approaching hazards without dying — fires for every spike
        float dist = state.nearestHazardDist;
        float reward = 0.0f;
        if (dist < 300.0f && !state.isDead) {
            // Reward for closing distance to the next spike
            if (dist < prevDist_) {
                float proximity = 1.0f - (dist / 300.0f);
                reward = proximity * 0.05f;
            }
        }
        prevDist_ = dist;
        return reward;
    }
    std::string GetName() const override { return "Proximity"; }
};

// ============================================================================
// Weighted reward combiner (like GigaLearn's reward weighting)
// ============================================================================

struct WeightedReward {
    std::unique_ptr<RewardFunction> reward;
    float weight;
    std::string name;
};

class RewardManager {
public:
    void addReward(std::unique_ptr<RewardFunction> reward, float weight) {
        std::string name = reward->GetName();
        rewards_.push_back({std::move(reward), weight, name});
    }

    float computeReward(const GameState& state, const GameState& prevState, bool isFinal) {
        float total = 0.0f;
        for (auto& wr : rewards_) {
            float r = wr.reward->GetReward(state, prevState, isFinal);
            wr.lastValue = r * wr.weight;
            total += wr.lastValue;
        }
        totalReward_ = total;
        return total;
    }

    void reset() {
        for (auto& wr : rewards_) {
            wr.reward->Reset();
        }
    }

    void printBreakdown() const {
        std::cout << "  Reward breakdown:";
        for (const auto& wr : rewards_) {
            std::cout << " [" << wr.name << ": " << wr.lastValue << "]";
        }
        std::cout << " Total: " << totalReward_ << std::endl;
    }

    size_t size() const { return rewards_.size(); }

private:
    struct WeightedRewardEntry {
        std::unique_ptr<RewardFunction> reward;
        float weight;
        std::string name;
        float lastValue = 0.0f;
    };
    std::vector<WeightedRewardEntry> rewards_;
    float totalReward_ = 0.0f;
};

// Factory function to create default reward setup
RewardManager createDefaultRewards();

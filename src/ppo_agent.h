#pragma once

#include "neural_net.h"
#include "config.h"
#include <vector>
#include <random>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <iostream>

// ============================================================================
// PPO Experience buffer — stores rollout data for training
// ============================================================================
struct Experience {
    std::vector<float> obs;
    int action;
    float reward;
    float value;       // V(s) from critic
    float logProb;     // log pi(a|s) from policy
    bool done;
};

struct Batch {
    std::vector<std::vector<float>> obs;
    std::vector<int> actions;
    std::vector<float> returns;     // discounted returns
    std::vector<float> advantages;  // GAE advantages
    std::vector<float> oldLogProbs;
    std::vector<float> oldValues;
};

// ============================================================================
// PPO Agent — Proximal Policy Optimization (like GigaLearn's PPO)
// Dual network: policy (actor) + value (critic)
// ============================================================================
class PPOAgent {
public:
    PPOAgent();

    void init(int obsSize, int actionSize);

    // Select action given observation (stochastic for training)
    int selectAction(const std::vector<float>& obs, float& logProb, float& value);

    // Select action deterministically (for inference/play)
    int selectActionGreedy(const std::vector<float>& obs);

    // Store experience
    void storeExperience(const Experience& exp);

    // Train on collected experiences (PPO update)
    // Returns: {policy_loss, value_loss, entropy}
    std::vector<float> update();

    // Check if buffer is full (ready to train)
    bool isBufferFull() const;

    void clearBuffer();

    // Save / Load
    void save(const std::string& dir) const;
    void save(const std::string& dir, int levelIdx) const;
    bool load(const std::string& dir);
    bool load(const std::string& dir, int& outLevelIdx);

    int getTotalSteps() const { return totalSteps_; }
    int getUpdateCount() const { return updateCount_; }

    const NeuralNet& getPolicy() const { return policy_; }
    const NeuralNet& getCritic() const { return critic_; }

private:
    NeuralNet policy_;   // Actor: obs -> action probabilities
    NeuralNet critic_;   // Critic: obs -> state value

    std::vector<Experience> buffer_;
    std::mt19937 rng_{42};

    int obsSize_ = 0;
    int actionSize_ = 0;
    int totalSteps_ = 0;
    int updateCount_ = 0;

    // Pre-allocated buffer for softmax probs (avoids alloc in hot loop)
    std::vector<float> probsBuf_;

    // Compute GAE (Generalized Advantage Estimation)
    Batch computeGAE();

    // Single PPO epoch on a mini-batch
    void trainMiniBatch(const Batch& batch, int startIdx, int endIdx,
                        float& policyLoss, float& valueLoss, float& entropy);
};

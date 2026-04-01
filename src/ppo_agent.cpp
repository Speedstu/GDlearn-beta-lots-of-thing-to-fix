#include "ppo_agent.h"
#include <filesystem>
#include <fstream>

PPOAgent::PPOAgent() {}

void PPOAgent::init(int obsSize, int actionSize) {
    obsSize_ = obsSize;
    actionSize_ = actionSize;

    // Policy network: obs -> hidden -> hidden -> hidden -> action_logits
    policy_.init({obsSize,
                  ppo_config::HIDDEN_SIZE_1,
                  ppo_config::HIDDEN_SIZE_2,
                  ppo_config::HIDDEN_SIZE_3,
                  actionSize}, 42);

    // Critic network: obs -> hidden -> hidden -> hidden -> 1 (value)
    critic_.init({obsSize,
                  ppo_config::HIDDEN_SIZE_1,
                  ppo_config::HIDDEN_SIZE_2,
                  ppo_config::HIDDEN_SIZE_3,
                  1}, 123);

    policy_.initBuffers();
    critic_.initBuffers();

    buffer_.reserve(ppo_config::STEPS_PER_UPDATE + 64);
    probsBuf_.resize(actionSize);
    std::cout << "[PPO] Initialized. Policy params: " << policy_.getParamCount()
              << ", Critic params: " << critic_.getParamCount() << std::endl;
}

int PPOAgent::selectAction(const std::vector<float>& obs, float& logProb, float& value) {
    auto logits = policy_.forward(obs);
    auto probs = NeuralNet::softmax(logits);

    auto valueOut = critic_.forward(obs);
    value = valueOut[0];

    int action = policy_.sampleAction(probs);
    logProb = std::log(probs[action] + 1e-8f);

    totalSteps_++;
    return action;
}

int PPOAgent::selectActionGreedy(const std::vector<float>& obs) {
    auto logits = policy_.forward(obs);
    auto probs = NeuralNet::softmax(logits);
    return (int)(std::max_element(probs.begin(), probs.end()) - probs.begin());
}

void PPOAgent::storeExperience(const Experience& exp) {
    buffer_.push_back(exp);
}

bool PPOAgent::isBufferFull() const {
    return (int)buffer_.size() >= ppo_config::STEPS_PER_UPDATE;
}

void PPOAgent::clearBuffer() {
    buffer_.clear();
}

// ============================================================================
// GAE (Generalized Advantage Estimation)
// Same algorithm as GigaLearn/RLGymPPO
// ============================================================================
Batch PPOAgent::computeGAE() {
    int n = (int)buffer_.size();
    Batch batch;
    batch.obs.resize(n);
    batch.actions.resize(n);
    batch.returns.resize(n);
    batch.advantages.resize(n);
    batch.oldLogProbs.resize(n);
    batch.oldValues.resize(n);

    // Copy data
    for (int i = 0; i < n; i++) {
        batch.obs[i] = buffer_[i].obs;
        batch.actions[i] = buffer_[i].action;
        batch.oldLogProbs[i] = buffer_[i].logProb;
        batch.oldValues[i] = buffer_[i].value;
    }

    // Compute GAE advantages
    float lastGAE = 0.0f;
    float lastValue = 0.0f;

    // Bootstrap value for last state
    if (!buffer_.back().done) {
        auto v = critic_.forward(buffer_.back().obs);
        lastValue = v[0];
    }

    for (int i = n - 1; i >= 0; i--) {
        float nextValue = (i == n - 1) ? lastValue : buffer_[i + 1].value;
        float nextDone = (i == n - 1) ? (buffer_[i].done ? 1.0f : 0.0f)
                                       : (buffer_[i + 1].done ? 1.0f : 0.0f);
        if (buffer_[i].done) nextDone = 1.0f;

        float delta = buffer_[i].reward
                    + ppo_config::GAMMA * nextValue * (1.0f - nextDone)
                    - buffer_[i].value;

        lastGAE = delta + ppo_config::GAMMA * ppo_config::GAE_LAMBDA
                        * (1.0f - nextDone) * lastGAE;

        if (buffer_[i].done) lastGAE = delta; // reset at episode boundary

        batch.advantages[i] = lastGAE;
        batch.returns[i] = lastGAE + buffer_[i].value;
    }

    // Normalize advantages
    float mean = 0.0f, var = 0.0f;
    for (float a : batch.advantages) mean += a;
    mean /= n;
    for (float a : batch.advantages) var += (a - mean) * (a - mean);
    var /= n;
    float stddev = std::sqrt(var + 1e-8f);
    for (float& a : batch.advantages) a = (a - mean) / stddev;

    return batch;
}

// ============================================================================
// PPO Training Update with Entropy Floor
// ============================================================================
void PPOAgent::trainMiniBatch(const Batch& batch, int startIdx, int endIdx,
                              float& policyLoss, float& valueLoss, float& entropy) {
    policyLoss = 0.0f;
    valueLoss = 0.0f;
    entropy = 0.0f;
    int count = endIdx - startIdx;
    if (count <= 0) return;
    float invCount = 1.0f / (float)count;

    policy_.zeroGrad();
    critic_.zeroGrad();

    for (int i = startIdx; i < endIdx; i++) {
        auto logits = policy_.forward(batch.obs[i]);
        auto probs = NeuralNet::softmax(logits);

        auto valueOut = critic_.forward(batch.obs[i]);
        float value = valueOut[0];

        int action = batch.actions[i];
        float newLogProb = std::log(probs[action] + 1e-8f);
        float oldLogProb = batch.oldLogProbs[i];
        float advantage = batch.advantages[i];
        float ret = batch.returns[i];

        // PPO clipped objective
        float ratio = std::exp(newLogProb - oldLogProb);
        float clippedRatio = std::clamp(ratio,
            1.0f - ppo_config::CLIP_EPSILON,
            1.0f + ppo_config::CLIP_EPSILON);

        float surr1 = ratio * advantage;
        float surr2 = clippedRatio * advantage;
        float pLoss = -std::min(surr1, surr2);
        policyLoss += pLoss;

        // Value loss (MSE)
        float vLoss = 0.5f * (value - ret) * (value - ret);
        valueLoss += vLoss;

        // Entropy (use clamped probs for gradient but raw for metric)
        float ent = 0.0f;
        for (float p : probs) {
            if (p > 1e-8f) ent -= p * std::log(p);
        }
        entropy += ent;

        // ---- Backprop policy ----
        // When ratio is within clip range, surr1==surr2, use unclipped gradient
        // Only zero gradient when ratio is outside clip range AND clipped is better
        float dLogProb;
        if (surr1 <= surr2) {
            dLogProb = -advantage * ratio; // unclipped path (includes ratio==1 case)
        } else {
            dLogProb = 0.0f; // clipped: ratio outside range and advantage would push further
        }

        int nActions = (int)logits.size();
        float uniformProb = 1.0f / nActions;
        std::vector<float> policyGrad(nActions, 0.0f);
        for (int j = 0; j < nActions; j++) {
            float dSoftmax = (j == action) ? (1.0f - probs[j]) : (-probs[j]);
            // PPO policy gradient
            policyGrad[j] = dLogProb * dSoftmax * invCount;
            // KL-from-uniform regularization: gradient of KL(pi || uniform)
            // = (p_j - 1/n), pushes probs toward uniform. Never vanishes.
            policyGrad[j] += ppo_config::ENTROPY_COEF * (probs[j] - uniformProb) * invCount;
        }

        policy_.backward(batch.obs[i], policyGrad);

        // ---- Backprop critic (averaged) ----
        std::vector<float> criticGrad = {ppo_config::VALUE_COEF * (value - ret) * invCount};
        critic_.backward(batch.obs[i], criticGrad);
    }

    // Clip gradients
    policy_.clipGrad(ppo_config::MAX_GRAD_NORM);
    critic_.clipGrad(ppo_config::MAX_GRAD_NORM);

    // Adam step
    policy_.adamStep(ppo_config::LEARNING_RATE);
    critic_.adamStep(ppo_config::LEARNING_RATE);

    policyLoss /= count;
    valueLoss /= count;
    entropy /= count;
}

std::vector<float> PPOAgent::update() {
    if (buffer_.empty()) return {0.0f, 0.0f, 0.0f};

    Batch batch = computeGAE();
    int n = (int)batch.obs.size();

    float totalPLoss = 0.0f, totalVLoss = 0.0f, totalEntropy = 0.0f;
    int totalBatches = 0;

    // Create shuffled indices
    std::vector<int> indices(n);
    std::iota(indices.begin(), indices.end(), 0);

    for (int epoch = 0; epoch < ppo_config::PPO_EPOCHS; epoch++) {
        // Shuffle
        std::shuffle(indices.begin(), indices.end(), rng_);

        // Reorder batch by shuffled indices
        Batch shuffled;
        shuffled.obs.resize(n);
        shuffled.actions.resize(n);
        shuffled.returns.resize(n);
        shuffled.advantages.resize(n);
        shuffled.oldLogProbs.resize(n);
        shuffled.oldValues.resize(n);

        for (int i = 0; i < n; i++) {
            int idx = indices[i];
            shuffled.obs[i] = batch.obs[idx];
            shuffled.actions[i] = batch.actions[idx];
            shuffled.returns[i] = batch.returns[idx];
            shuffled.advantages[i] = batch.advantages[idx];
            shuffled.oldLogProbs[i] = batch.oldLogProbs[idx];
            shuffled.oldValues[i] = batch.oldValues[idx];
        }

        // Mini-batch training
        for (int start = 0; start < n; start += ppo_config::MINI_BATCH_SIZE) {
            int end = std::min(start + ppo_config::MINI_BATCH_SIZE, n);
            float pl, vl, ent;
            trainMiniBatch(shuffled, start, end, pl, vl, ent);
            totalPLoss += pl;
            totalVLoss += vl;
            totalEntropy += ent;
            totalBatches++;
        }
    }

    updateCount_++;
    clearBuffer();

    if (totalBatches > 0) {
        totalPLoss /= totalBatches;
        totalVLoss /= totalBatches;
        totalEntropy /= totalBatches;
    }

    return {totalPLoss, totalVLoss, totalEntropy};
}

void PPOAgent::save(const std::string& dir, int levelIdx) const {
    std::filesystem::create_directories(dir);
    policy_.save(dir + "/policy.bin");
    critic_.save(dir + "/critic.bin");
    // Save level index metadata
    std::ofstream meta(dir + "/metadata.txt");
    if (meta.is_open()) {
        meta << levelIdx << "\n" << totalSteps_ << "\n" << updateCount_ << "\n";
        meta.close();
    }
    std::cout << "[PPO] Saved checkpoint to " << dir << " (level: " << levelIdx << ")" << std::endl;
}

void PPOAgent::save(const std::string& dir) const {
    save(dir, 0); // Default to level 0 for backward compatibility
}

bool PPOAgent::load(const std::string& dir, int& outLevelIdx) {
    bool ok = policy_.load(dir + "/policy.bin") && critic_.load(dir + "/critic.bin");
    if (ok) {
        const auto& policySizes = policy_.getLayerSizes();
        const auto& criticSizes = critic_.getLayerSizes();
        if (policySizes.empty() || criticSizes.empty() ||
            policySizes.front() != obsSize_ || criticSizes.front() != obsSize_) {
            std::cerr << "[PPO] Checkpoint architecture mismatch. Expected obs="
                      << obsSize_ << ", got policy obs="
                      << (policySizes.empty() ? -1 : policySizes.front())
                      << " critic obs="
                      << (criticSizes.empty() ? -1 : criticSizes.front())
                      << ". Skipping load." << std::endl;
            return false;
        }
        policy_.initBuffers();
        critic_.initBuffers();
        probsBuf_.resize(actionSize_);
        // Load level index metadata
        std::ifstream meta(dir + "/metadata.txt");
        if (meta.is_open()) {
            meta >> outLevelIdx;
            meta >> totalSteps_;
            meta >> updateCount_;
            meta.close();
        } else {
            outLevelIdx = 0;
        }
        std::cout << "[PPO] Loaded checkpoint from " << dir << " (level: " << outLevelIdx << ")" << std::endl;
    }
    return ok;
}

bool PPOAgent::load(const std::string& dir) {
    int dummyLevel = 0;
    return load(dir, dummyLevel);
}

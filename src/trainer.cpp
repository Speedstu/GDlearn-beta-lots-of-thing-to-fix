#include "trainer.h"
#include "memory_reader.h"
#include "input_injector.h"
#include "renderer.h"
#include <filesystem>
#include <chrono>
#include <thread>
#include <algorithm>
#include <numeric>
#include <ctime>

// ============================================================================
// Get current Unix timestamp as string (unique across sessions)
// ============================================================================
std::string getTimestampStr() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    return std::to_string((long long)t);
}

// ============================================================================
// Keep only the N most recent auto_* checkpoints, delete older ones
// ============================================================================
void cleanOldAutoCheckpoints(const std::string& checkpointDir, int keepCount = 5) {
    if (!std::filesystem::exists(checkpointDir)) return;

    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> autoCkpts;
    for (const auto& entry : std::filesystem::directory_iterator(checkpointDir)) {
        if (!entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        // Only clean auto_* checkpoints (keep latest, exit_*, completed_*)
        if (name.substr(0, 5) != "auto_") continue;
        auto policyFile = entry.path() / "policy.bin";
        if (!std::filesystem::exists(policyFile)) continue;
        autoCkpts.push_back({entry.last_write_time(), entry.path()});
    }

    // Sort oldest first
    std::sort(autoCkpts.begin(), autoCkpts.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    // Delete oldest, keep last `keepCount`
    int toDelete = (int)autoCkpts.size() - keepCount;
    for (int i = 0; i < toDelete; ++i) {
        std::error_code ec;
        std::filesystem::remove_all(autoCkpts[i].second, ec);
        if (!ec) {
            std::cout << "[Checkpoint] Cleaned old: " << autoCkpts[i].second.filename().string() << std::endl;
        }
    }
}

Trainer::Trainer() {}

void Trainer::addLevel(const LevelData& level) {
    levels_.push_back(level);
    std::cout << "[Trainer] Added level: " << level.name
              << " (" << level.objectCount << " objects, length="
              << level.totalLength << ")" << std::endl;
}

void Trainer::addLevelFromFile(const std::string& filepath) {
    auto level = LevelParser::parseFromFile(filepath);
    if (level.objectCount > 0) {
        level.name = filepath;
        addLevel(level);
    }
}

void Trainer::addTestLevel(int difficulty) {
    addLevel(LevelParser::createTestLevel(difficulty));
}

// ============================================================================
// Run a single episode, return progress %
// greedy=true: argmax action (for PPO eval / live play)
// greedy=false: stochastic sampling (for genetic — creates behavioral diversity)
// ============================================================================
// ============================================================================
// Find the most recent checkpoint by modification time
// Returns path to checkpoint dir (or empty if none found)
// ============================================================================
std::string findMostRecentCheckpoint(const std::string& checkpointDir) {
    if (!std::filesystem::exists(checkpointDir)) {
        return "";
    }
    
    std::filesystem::path mostRecent;
    auto mostRecentTime = std::filesystem::file_time_type::min();
    bool found = false;
    
    for (const auto& entry : std::filesystem::directory_iterator(checkpointDir)) {
        if (entry.is_directory()) {
            // Sort by policy.bin modification time (NOT folder time)
            // Folder time can be touched by OS reads/scans
            auto policyFile = entry.path() / "policy.bin";
            if (std::filesystem::exists(policyFile)) {
                auto modTime = std::filesystem::last_write_time(policyFile);
                if (!found || modTime > mostRecentTime) {
                    mostRecentTime = modTime;
                    mostRecent = entry.path();
                    found = true;
                }
            }
        }
    }
    
    if (found) {
        std::cout << "[Checkpoint] Loading most recent: " << mostRecent.filename().string() << std::endl;
        return mostRecent.string();
    }
    
    // Fallback to "latest" if exists
    auto latest = std::filesystem::path(checkpointDir) / "latest";
    if (std::filesystem::exists(latest)) {
        return latest.string();
    }
    
    return "";
}

float Trainer::runEpisode(Environment& env, NeuralNet& policy, bool greedy) {
    auto obs = env.reset();
    float totalReward = 0.0f;
    int stepsTaken = 0;

    for (int step = 0; step < env_config::MAX_EPISODE_STEPS; step++) {
        auto logits = policy.forward(obs);
        auto probs = NeuralNet::softmax(logits);

        int action;
        if (greedy) {
            action = (int)(std::max_element(probs.begin(), probs.end()) - probs.begin());
        } else {
            // Stochastic: the net's weights determine the probability distribution
            // Different nets → different action distributions → behavioral diversity
            action = policy.sampleAction(probs);
        }

        auto result = env.step(action);
        obs = result.obs;
        totalReward += result.reward;
        stepsTaken++;

        if (result.done) break;
    }

    // Fitness = progress + survival bonus (survive longer = better, even if same progress)
    float progress = env.getSim().getProgressPercent();
    float survivalBonus = stepsTaken * 0.001f; // small bonus for surviving longer
    return progress + survivalBonus;
}

// ============================================================================
// Evaluate model on all loaded levels
// ============================================================================
float Trainer::evaluate(NeuralNet& net, bool verbose) {
    float totalProgress = 0.0f;
    RewardManager rewards = createDefaultRewards();

    for (size_t i = 0; i < levels_.size(); i++) {
        Environment env;
        env.loadLevel(levels_[i]);
        env.setRewards(createDefaultRewards());

        float progress = runEpisode(env, net, true);
        totalProgress += progress;

        if (verbose) {
            std::cout << "  Level " << i << " (" << levels_[i].name
                      << "): " << progress << "%" << std::endl;
        }
    }

    return levels_.empty() ? 0.0f : totalProgress / levels_.size();
}

// ============================================================================
// Main training dispatch
// ============================================================================
void Trainer::train(int totalTimesteps) {
    if (levels_.empty()) {
        std::cerr << "[Trainer] No levels loaded! Add levels first." << std::endl;
        return;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "  GDLearnCPP Training" << std::endl;
    std::cout << "  Mode: " << (mode_ == TrainMode::PPO ? "PPO" :
                                mode_ == TrainMode::GENETIC ? "Genetic" : "Hybrid") << std::endl;
    std::cout << "  Levels: " << levels_.size() << std::endl;
    std::cout << "  Timesteps: " << totalTimesteps << std::endl;
    std::cout << "========================================\n" << std::endl;

    switch (mode_) {
        case TrainMode::PPO:
            if (infiniteMode_) {
                if (visualMode_) {
                    trainInfiniteVisual();
                } else {
                    trainInfiniteNoVisual();
                }
            } else if (visualMode_) {
                trainPPOVisual(totalTimesteps);
            } else {
                trainPPO(totalTimesteps);
            }
            break;
        case TrainMode::GENETIC:
            trainGeneticImpl(geneticGenerations_, geneticPopulation_);
            break;
        case TrainMode::HYBRID:
            // Phase 1: Genetic warmup
            std::cout << "=== Phase 1: Genetic warmup ===" << std::endl;
            trainGeneticImpl(geneticGenerations_, geneticPopulation_);

            // Phase 2: PPO fine-tuning
            std::cout << "\n=== Phase 2: PPO fine-tuning ===" << std::endl;
            if (infiniteMode_) {
                if (visualMode_) {
                    trainInfiniteVisual();
                } else {
                    trainInfiniteNoVisual();
                }
            } else if (visualMode_) {
                trainPPOVisual(totalTimesteps);
            } else {
                trainPPO(totalTimesteps);
            }
            break;
    }
}

// ============================================================================
// PPO Training Loop
// ============================================================================
void Trainer::trainPPO(int totalTimesteps) {
    logger_.init(logDir_);

    // Create environment (cycles through levels)
    Environment env;
    env.loadLevel(levels_[0]);
    env.setRewards(createDefaultRewards());

    // Create PPO agent
    PPOAgent agent;
    agent.init(env.getObsSize(), env.getActionSize());

    // Try to load most recent checkpoint
    auto recentCheckpoint = findMostRecentCheckpoint(checkpointDir_);
    if (!recentCheckpoint.empty()) {
        agent.load(recentCheckpoint);
    }

    int currentLevel = 0;
    int episodes = 0;
    float bestProgress = 0.0f;
    auto obs = env.reset();

    // Rolling stats for recent episodes
    std::vector<float> recentProgress;
    std::vector<float> recentRewards;
    float episodeReward = 0.0f;
    int clickCount = 0;
    int stepsSinceUpdate = 0;

    std::cout << "[PPO] Starting training. Obs size: " << env.getObsSize()
              << ", Action size: " << env.getActionSize()
              << ", Policy params: " << agent.getPolicy().getParamCount()
              << std::endl;

    auto startTime = std::chrono::steady_clock::now();

    while (agent.getTotalSteps() < totalTimesteps) {
        // Collect experience
        float logProb, value;
        int action = agent.selectAction(obs, logProb, value);
        if (action == 1) clickCount++;

        auto result = env.step(action);

        Experience exp;
        exp.obs = obs;
        exp.action = action;
        exp.reward = result.reward;
        exp.value = value;
        exp.logProb = logProb;
        exp.done = result.done;
        agent.storeExperience(exp);

        logger_.logStep(result.reward, result.progress, result.done);

        obs = result.obs;
        episodeReward += result.reward;
        stepsSinceUpdate++;

        if (result.done) {
            episodes++;
            if (result.progress > bestProgress) {
                bestProgress = result.progress;
            }

            // Track rolling stats (last 50 episodes)
            recentProgress.push_back(result.progress);
            recentRewards.push_back(episodeReward);
            if (recentProgress.size() > 50) {
                recentProgress.erase(recentProgress.begin());
                recentRewards.erase(recentRewards.begin());
            }
            episodeReward = 0.0f;

            // Cycle to next level periodically
            if (levels_.size() > 1 && episodes % 10 == 0) {
                currentLevel = (currentLevel + 1) % (int)levels_.size();
                env.loadLevel(levels_[currentLevel]);
                env.setRewards(createDefaultRewards());
            }
            obs = env.reset();
        }

        // PPO update when buffer is full
        if (agent.isBufferFull()) {
            auto losses = agent.update();

            // Compute rolling averages
            float avgProg = 0.0f, avgRew = 0.0f;
            for (float p : recentProgress) avgProg += p;
            for (float r : recentRewards) avgRew += r;
            if (!recentProgress.empty()) {
                avgProg /= recentProgress.size();
                avgRew /= recentRewards.size();
            }

            float clickRate = (stepsSinceUpdate > 0) ?
                (float)clickCount / stepsSinceUpdate * 100.0f : 0.0f;

            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - startTime).count();
            float stepsPerSec = agent.getTotalSteps() / std::max(0.01f, elapsed);

            std::cout << "===== Update #" << agent.getUpdateCount()
                      << " | Steps: " << agent.getTotalSteps()
                      << " | Time: " << (int)elapsed << "s =====" << std::endl;
            std::cout << "  Episodes: " << episodes
                      << " | Steps/s: " << (int)stepsPerSec
                      << " | Click%: " << (int)clickRate << "%" << std::endl;
            std::cout << "  Avg Progress: " << avgProg << "%"
                      << " | Best: " << bestProgress << "%"
                      << " | Avg Reward: " << avgRew << std::endl;
            std::cout << "  P.Loss: " << losses[0]
                      << " | V.Loss: " << losses[1]
                      << " | Entropy: " << losses[2] << std::endl;
            std::cout << std::endl;

            clickCount = 0;
            stepsSinceUpdate = 0;

            logger_.logUpdate(agent.getTotalSteps(), agent.getUpdateCount(),
                            episodes, bestProgress,
                            losses[0], losses[1], losses[2]);

            // Save checkpoint
            if (agent.getUpdateCount() % 10 == 0) {
                agent.save(checkpointDir_ + "/latest");
            }
            if (agent.getUpdateCount() % 50 == 0) {
                agent.save(checkpointDir_ + "/step_" +
                          std::to_string(agent.getTotalSteps()));
            }
        }
    }

    agent.save(checkpointDir_ + "/final");

    auto endTime = std::chrono::steady_clock::now();
    float totalSec = std::chrono::duration<float>(endTime - startTime).count();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  PPO Training Complete!" << std::endl;
    std::cout << "  Total steps: " << agent.getTotalSteps() << std::endl;
    std::cout << "  Total episodes: " << episodes << std::endl;
    std::cout << "  Best progress: " << bestProgress << "%" << std::endl;
    std::cout << "  Time: " << totalSec << "s" << std::endl;
    std::cout << "  Speed: " << (int)(agent.getTotalSteps() / totalSec) << " steps/s" << std::endl;
    std::cout << "========================================\n" << std::endl;

    logger_.close();
}

// ============================================================================
// Visual PPO Training Loop — with real-time renderer
// ============================================================================
void Trainer::trainPPOVisual(int totalTimesteps) {
    logger_.init(logDir_);

    // Create renderer
    Renderer renderer(1280, 720);
    if (!renderer.createWindow("GDLearnCPP Training Visualizer")) {
        std::cerr << "[Trainer] Failed to create renderer, falling back to non-visual mode" << std::endl;
        trainPPO(totalTimesteps);
        return;
    }

    // Create environment
    Environment env;
    env.loadLevel(levels_[0]);
    env.setRewards(createDefaultRewards());

    // Create PPO agent
    PPOAgent agent;
    agent.init(env.getObsSize(), env.getActionSize());

    // Try to load most recent checkpoint
    auto recentCheckpoint = findMostRecentCheckpoint(checkpointDir_);
    if (!recentCheckpoint.empty()) {
        agent.load(recentCheckpoint);
    }

    int currentLevel = 0;
    int episodes = 0;
    float bestProgress = 0.0f;
    auto obs = env.reset();

    // Rolling stats
    std::vector<float> recentProgress;
    std::vector<float> recentRewards;
    float episodeReward = 0.0f;
    int clickCount = 0;
    int stepsSinceUpdate = 0;

    // Render state
    RenderState rstate;
    rstate.step = 0;
    rstate.episode = 0;

    std::cout << "[PPO] Starting visual training. Obs size: " << env.getObsSize()
              << ", Action size: " << env.getActionSize()
              << ", Policy params: " << agent.getPolicy().getParamCount()
              << std::endl;
    std::cout << "[Renderer] Press ESC to exit visual mode" << std::endl;

    auto startTime = std::chrono::steady_clock::now();

    while (agent.getTotalSteps() < totalTimesteps && !renderer.shouldClose()) {
        // Poll window events
        renderer.pollEvents();

        // Collect experience
        float logProb, value;
        int action = agent.selectAction(obs, logProb, value);
        if (action == 1) clickCount++;

        auto result = env.step(action);

        Experience exp;
        exp.obs = obs;
        exp.action = action;
        exp.reward = result.reward;
        exp.value = value;
        exp.logProb = logProb;
        exp.done = result.done;
        agent.storeExperience(exp);

        logger_.logStep(result.reward, result.progress, result.done);

        // Update render state
        rstate.playerX = result.state.playerX;
        rstate.playerY = result.state.playerY;
        rstate.playerSpeed = result.state.playerSpeed;
        rstate.percent = result.progress;
        rstate.isDead = result.state.isDead;
        rstate.isOnGround = result.state.isOnGround;
        rstate.isHolding = (action == 1);
        rstate.gameMode = result.state.gameMode;
        rstate.reward = result.reward;
        rstate.value = value;
        rstate.episode = episodes;
        rstate.step = agent.getTotalSteps();

        // Render
        renderer.renderLevel(currentLevel_.objects.empty() ? levels_[0] : currentLevel_, rstate);
        renderer.syncFrame();

        obs = result.obs;
        episodeReward += result.reward;
        stepsSinceUpdate++;

        if (result.done) {
            episodes++;
            if (result.progress > bestProgress) {
                bestProgress = result.progress;
            }

            recentProgress.push_back(result.progress);
            recentRewards.push_back(episodeReward);
            if (recentProgress.size() > 50) {
                recentProgress.erase(recentProgress.begin());
                recentRewards.erase(recentRewards.begin());
            }
            episodeReward = 0.0f;

            if (levels_.size() > 1 && episodes % 10 == 0) {
                currentLevel = (currentLevel + 1) % (int)levels_.size();
                env.loadLevel(levels_[currentLevel]);
                env.setRewards(createDefaultRewards());
            }
            obs = env.reset();
            rstate.trail.clear();
        }

        // PPO update when buffer is full
        if (agent.isBufferFull()) {
            auto losses = agent.update();

            float avgProg = 0.0f, avgRew = 0.0f;
            for (float p : recentProgress) avgProg += p;
            for (float r : recentRewards) avgRew += r;
            if (!recentProgress.empty()) {
                avgProg /= recentProgress.size();
                avgRew /= recentRewards.size();
            }

            float clickRate = (stepsSinceUpdate > 0) ?
                (float)clickCount / stepsSinceUpdate * 100.0f : 0.0f;

            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - startTime).count();
            float stepsPerSec = agent.getTotalSteps() / std::max(0.01f, elapsed);

            std::cout << "===== Update #" << agent.getUpdateCount()
                      << " | Steps: " << agent.getTotalSteps()
                      << " | Time: " << (int)elapsed << "s =====" << std::endl;
            std::cout << "  Episodes: " << episodes
                      << " | Steps/s: " << (int)stepsPerSec
                      << " | Click%: " << (int)clickRate << "%" << std::endl;
            std::cout << "  Avg Progress: " << avgProg << "%"
                      << " | Best: " << bestProgress << "%"
                      << " | Avg Reward: " << avgRew << std::endl;
            std::cout << "  P.Loss: " << losses[0]
                      << " | V.Loss: " << losses[1]
                      << " | Entropy: " << losses[2] << std::endl;
            std::cout << std::endl;

            clickCount = 0;
            stepsSinceUpdate = 0;

            logger_.logUpdate(agent.getTotalSteps(), agent.getUpdateCount(),
                            episodes, bestProgress,
                            losses[0], losses[1], losses[2]);

            if (agent.getUpdateCount() % 10 == 0) {
                agent.save(checkpointDir_ + "/latest");
            }
            if (agent.getUpdateCount() % 50 == 0) {
                agent.save(checkpointDir_ + "/step_" +
                          std::to_string(agent.getTotalSteps()));
            }
        }
    }

    agent.save(checkpointDir_ + "/final");
    logger_.close();
    std::cout << "[Renderer] Visual training complete" << std::endl;
}

// ============================================================================
// Genetic Algorithm Training (like the video's approach but on steroids)
// Population of neural nets, evaluated on sim, best survive and mutate.
// This is EXTREMELY fast because each evaluation is just running the sim.
// ============================================================================
void Trainer::trainGeneticImpl(int generations, int populationSize) {
    logger_.init(logDir_);

    // Pre-create one environment (reused for all evaluations)
    Environment env;
    env.loadLevel(levels_[0]);
    env.setRewards(createDefaultRewards());
    int obsSize = env.getObsSize();

    // Genetic mode: medium network 128→64 (~27K params)
    // Big enough to learn complex patterns, small enough for mutation search.
    // PPO later uses the full 256→256→128 for superhuman play.
    std::vector<int> netShape = {obsSize, 128, 64, 2};

    // Initialize population
    std::vector<NeuralNet> population(populationSize);
    std::vector<float> fitness(populationSize, 0.0f);
    std::mt19937 rng(42);

    for (int i = 0; i < populationSize; i++) {
        population[i].init(netShape, 42 + i);
    }

    float bestEverProgress = 0.0f;
    NeuralNet bestEverNet;
    bestEverNet.init(netShape, 0);

    auto startTime = std::chrono::steady_clock::now();

    std::cout << "[Genetic] Population: " << populationSize
              << ", Generations: " << generations
              << ", Net params: " << population[0].getParamCount()
              << ", Levels: " << levels_.size() << std::endl;

    for (int gen = 0; gen < generations; gen++) {
        // Pick one level per generation (rotate), keeps eval fast
        int lvIdx = gen % (int)levels_.size();
        env.loadLevel(levels_[lvIdx]);
        env.setRewards(createDefaultRewards());

        // Evaluate all individuals on this generation's level
        for (int i = 0; i < populationSize; i++) {
            fitness[i] = runEpisode(env, population[i], false); // stochastic for diversity
        }

        // Find best
        int bestIdx = (int)(std::max_element(fitness.begin(), fitness.end()) - fitness.begin());
        float bestFit = fitness[bestIdx];

        if (bestFit > bestEverProgress) {
            bestEverProgress = bestFit;
            bestEverNet = population[bestIdx];
            bestEverNet.save(checkpointDir_ + "/genetic_best.bin");
        }

        // Log every 10 generations (or every gen for first 50)
        if (gen % 10 == 0 || gen < 50 || gen == generations - 1) {
            float avgFit = 0.0f;
            for (float f : fitness) avgFit += f;
            avgFit /= populationSize;

            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - startTime).count();
            float genPerSec = (elapsed > 0) ? (gen + 1) / elapsed : 0.0f;

            std::cout << "Gen " << gen << "/" << generations
                      << " | Lv" << lvIdx
                      << " | Best: " << bestFit << "%"
                      << " | Avg: " << avgFit << "%"
                      << " | All-time: " << bestEverProgress << "%"
                      << " | " << genPerSec << " gen/s"
                      << std::endl;
        }

        // ---- Selection + mutation for next generation ----
        // Sort by fitness
        std::vector<std::pair<float, int>> sortedFitness;
        sortedFitness.reserve(populationSize);
        for (int i = 0; i < populationSize; i++) {
            sortedFitness.push_back({fitness[i], i});
        }
        std::sort(sortedFitness.begin(), sortedFitness.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });

        std::vector<NeuralNet> newPop(populationSize);
        int eliteCount = std::max(1, populationSize / 20); // top 5%

        // Keep elites unchanged
        for (int i = 0; i < eliteCount; i++) {
            newPop[i] = population[sortedFitness[i].second];
        }

        // Fill rest with mutations of top 20% performers
        int topN = std::max(2, populationSize / 5);
        std::uniform_int_distribution<int> topDist(0, topN - 1);

        for (int i = eliteCount; i < populationSize; i++) {
            int parentIdx = sortedFitness[topDist(rng)].second;

            // Adaptive mutation: less mutation as fitness improves
            float progressFrac = bestEverProgress / 100.0f;
            float mutRate = 0.20f * (1.0f - progressFrac) + 0.03f;
            float mutScale = 0.40f * (1.0f - progressFrac) + 0.05f;

            // Crossover every 5th individual
            if (i % 5 == 0) {
                int parent2Idx = sortedFitness[topDist(rng)].second;
                newPop[i] = NeuralNet::crossover(population[parentIdx],
                                                  population[parent2Idx])
                             .mutate(mutRate * 0.5f, mutScale * 0.5f);
            } else {
                newPop[i] = population[parentIdx].mutate(mutRate, mutScale);
            }
        }

        population = std::move(newPop);

        // Early exit if level completed
        if (bestEverProgress >= 100.0f) {
            std::cout << "\n!!! LEVEL COMPLETED at generation " << gen << " !!!" << std::endl;
            break;
        }
    }

    // Final evaluation of best net on ALL levels
    if (levels_.size() > 1) {
        std::cout << "\nFinal evaluation on all " << levels_.size() << " levels:" << std::endl;
        for (size_t lv = 0; lv < levels_.size(); lv++) {
            env.loadLevel(levels_[lv]);
            env.setRewards(createDefaultRewards());
            float prog = runEpisode(env, bestEverNet, true);
            std::cout << "  Level " << lv << ": " << prog << "%" << std::endl;
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    float totalSec = std::chrono::duration<float>(endTime - startTime).count();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Genetic Training Complete!" << std::endl;
    std::cout << "  Generations: " << generations << std::endl;
    std::cout << "  Best progress: " << bestEverProgress << "%" << std::endl;
    std::cout << "  Time: " << totalSec << "s" << std::endl;
    std::cout << "  Speed: " << generations / std::max(0.01f, totalSec) << " gen/s" << std::endl;
    std::cout << "========================================\n" << std::endl;

    bestEverNet.save(checkpointDir_ + "/genetic_best.bin");
    logger_.close();
}

// ============================================================================
// Live play mode — read from real GD + send inputs
// ============================================================================
void Trainer::playLive(const std::string& checkpointPath) {
    // PPO saves checkpoints as directories with policy.bin + critic.bin
    // For inference we only need the policy (actor)
    std::string policyPath = checkpointPath;
    
    // Check if it's a directory - if so, append policy.bin
    if (std::filesystem::is_directory(checkpointPath)) {
        policyPath = checkpointPath + "/policy.bin";
    }
    
    std::cout << "[Live] Loading policy from " << policyPath << std::endl;

    NeuralNet policy;
    if (!policy.load(policyPath)) {
        std::cerr << "[Live] Failed to load checkpoint!" << std::endl;
        std::cerr << "[Live] Expected: " << policyPath << std::endl;
        std::cerr << "[Live] Make sure you've trained with 'train --infinite' first" << std::endl;
        return;
    }

    MemoryReader reader;
    if (!reader.attach()) {
        std::cerr << "[Live] Failed to attach to GD process!" << std::endl;
        return;
    }

    InputInjector input;
    if (!input.findWindow()) {
        std::cerr << "[Live] Failed to find GD window!" << std::endl;
        return;
    }

    std::cout << "[Live] Playing on real GD! Press Ctrl+C to stop." << std::endl;
    std::cout << "[Live] Start a level in GD and the bot will play." << std::endl;

    // We need an environment just for building observations
    // Load the SAME level as in GD (Stereo Madness) so grid scan works
    Environment env;
    LevelData stereoMadness = LevelParser::parseFromFile("G:/gd-ml-bot/levels/1.gmd");
    if (stereoMadness.objectCount == 0) {
        std::cerr << "[Live] Failed to load Stereo Madness level!" << std::endl;
        std::cerr << "[Live] Make sure G:/gd-ml-bot/levels/1.gmd exists" << std::endl;
        return;
    }
    env.loadLevel(stereoMadness);
    env.setRewards(createDefaultRewards());

    GameState prevState{};
    bool wasPlaying = false;

    while (true) {
        GameState state;
        if (!reader.readGameState(state)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (!state.isPlaying) {
            if (wasPlaying) {
                std::cout << "[Live] Level ended. Waiting for next attempt..." << std::endl;
                input.releaseClick();
            }
            wasPlaying = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (!wasPlaying) {
            std::cout << "[Live] Level started! Bot is playing..." << std::endl;
            wasPlaying = true;
        }

        // Build observation from live game state - NOW WITH GRID SCAN!
        std::vector<float> obs = env.buildObservationFromState(state);

        // Get action
        auto logits = policy.forward(obs);
        auto probs = NeuralNet::softmax(logits);
        int action = (int)(std::max_element(probs.begin(), probs.end()) - probs.begin());

        // Execute action - handle both pulse (cube/ball) and hold (ship/UFO/wave) modes
        static bool wasHolding = false;
        bool shouldHold = (action == 1);
        int gameMode = state.gameMode;
        
        // Ship (1), UFO (3), Wave (4), Swing (7) require continuous holding
        bool isHoldMode = (gameMode == 1 || gameMode == 3 || gameMode == 4 || gameMode == 7);
        
        if (isHoldMode) {
            // Hold mode: press when action=1, release when action=0
            if (shouldHold && !wasHolding) {
                input.pressClick();
                wasHolding = true;
            } else if (!shouldHold && wasHolding) {
                input.releaseClick();
                wasHolding = false;
            }
            // While holding, keep it pressed (no-op here, pressClick keeps it down)
        } else {
            // Pulse mode (cube, ball, robot, spider): click pulse on transition to hold
            if (shouldHold && !wasHolding) {
                input.click(50); // 50ms click pulse for jump
                wasHolding = true;
            } else if (!shouldHold && wasHolding) {
                wasHolding = false;
            }
        }

        prevState = state;

        // Run at game speed (~60fps)
        std::this_thread::sleep_for(std::chrono::microseconds(16667));
    }
}

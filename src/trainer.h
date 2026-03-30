#pragma once

#include "environment.h"
#include "ppo_agent.h"
#include "neural_net.h"
#include "logger.h"
#include "level_parser.h"
#include <string>
#include <vector>
#include <memory>

// ============================================================================
// Training modes
// ============================================================================
enum class TrainMode {
    PPO,        // PPO reinforcement learning (gradient-based)
    GENETIC,    // Genetic algorithm / evolutionary (mutation-based)
    HYBRID      // PPO + periodic genetic tournament
};

// ============================================================================
// Trainer: orchestrates training loop at maximum speed
// Runs the simulator internally — no real-time game needed for training.
// Like GigaLearn's Learner but for GD.
// ============================================================================
class Renderer;  // Forward declaration

class Trainer {
public:
    Trainer();

    // Load level(s) for training
    void addLevel(const LevelData& level);
    void addLevelFromFile(const std::string& filepath);
    void addTestLevel(int difficulty);

    // Configure
    void setMode(TrainMode mode) { mode_ = mode; }
    void setCheckpointDir(const std::string& dir) { checkpointDir_ = dir; }
    void setLogDir(const std::string& dir) { logDir_ = dir; }
    void setVisualMode(bool visual) { visualMode_ = visual; }
    void setVisualFPS(int fps) { visualFPS_ = fps; }
    void setInfiniteMode(bool infinite) { infiniteMode_ = infinite; }

    void setGeneticParams(int generations, int populationSize) {
        geneticGenerations_ = generations;
        geneticPopulation_ = populationSize;
    }

    // Run training
    void train(int totalTimesteps = ppo_config::TOTAL_TIMESTEPS);

    // Run genetic algorithm training
    void trainGenetic(int generations = 5000, int populationSize = 1000);

    // Get loaded level count
    int levelCount() const { return (int)levels_.size(); }

    // Evaluate a model on all loaded levels
    float evaluate(NeuralNet& net, bool verbose = false);

    // Load and play on real GD (live mode)
    void playLive(const std::string& checkpointPath);

private:
    std::vector<LevelData> levels_;
    TrainMode mode_ = TrainMode::HYBRID;
    std::string checkpointDir_ = "G:/gd-ml-bot/checkpoints";
    std::string logDir_ = "G:/gd-ml-bot/logs";
    int geneticGenerations_ = 1000;
    int geneticPopulation_ = 500;
    Logger logger_;
    
    // Visual mode
    bool visualMode_ = false;
    int visualFPS_ = 60;
    
    // Infinite training mode
    bool infiniteMode_ = false;
    
    // Current level for rendering
    LevelData currentLevel_;

    // PPO training loop
    void trainPPO(int totalTimesteps);
    
    // Visual training loop
    void trainPPOVisual(int totalTimesteps);

    // Infinite training mode - train until completion, auto-advance, auto-save
    void trainInfiniteVisual();
    void trainInfiniteNoVisual();

    // Genetic training loop
    void trainGeneticImpl(int generations, int populationSize);

    // Evaluate one episode on one level, return progress %
    float runEpisode(Environment& env, NeuralNet& policy, bool greedy);
};

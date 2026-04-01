/*
 * Infinite Training Mode with Auto-save and Level Progression
 * Train until level completion, then advance to harder levels
 */

#include "trainer.h"
#include "level_parser.h"
#include "ppo_agent.h"
#include "renderer.h"
#include "state_writer.h"
#include <iostream>
#include <chrono>
#include <csignal>
#include <atomic>

// Global flag for graceful shutdown
static std::atomic<bool> g_shouldStop{false};

// Forward declaration - defined in trainer.cpp
extern std::string findMostRecentCheckpoint(const std::string& checkpointDir);
extern std::string getTimestampStr();
extern void cleanOldAutoCheckpoints(const std::string& checkpointDir, int keepCount);

// Signal handler for Ctrl+C
void signalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\n[TRAINER] Received signal " << sig << ", saving checkpoint..." << std::endl;
        g_shouldStop = true;
    }
}

// ============================================================================
// INFINITE TRAINING MODE - Train until level completion, auto-advance, auto-save
// ============================================================================
void Trainer::trainInfiniteVisual() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  INFINITE TRAINING MODE" << std::endl;
    std::cout << "  Complete level 3x consecutively -> advance" << std::endl;
    std::cout << "  Auto-save every 60 seconds" << std::endl;
    std::cout << "  Press Ctrl+C or Q to stop" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Setup signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Create renderer
    Renderer renderer(1280, 720);
    if (!renderer.createWindow("GDLearnCPP - Infinite Training")) {
        std::cerr << "[Trainer] Failed to create renderer" << std::endl;
        return;
    }

    logger_.init(logDir_);

    // Level progression: start easy, get harder
    int currentLevelIdx = 1;  // Start at Back On Track (after Stereo Madness)
    int levelsCompleted = 0;
    bool levelCompleted = false;
    int consecutiveCompletions = 0;  // Count consecutive level completions without dying

    // All 21 official GD levels in order of difficulty (starting with Stereo Madness)
    std::vector<std::string> levelOrder = {
        "stereomadness",    // Level 1: Stereo Madness
        "backontrack",      // Level 2: Back On Track
        "polargeist",       // Level 3: Polargeist
        "dryout",           // Level 4: Dry Out
        "baseafterbase",    // Level 5: Base After Base
        "cantletgo",        // Level 6: Can't Let Go
        "jumper",           // Level 7: Jumper
        "timemachine",      // Level 8: Time Machine
        "cycles",           // Level 9: Cycles
        "xstep",            // Level 10: xStep
        "clutterfunk",      // Level 11: Clutterfunk
        "toe",              // Level 12: Theory of Everything
        "electroman",       // Level 13: Electroman Adventures
        "clubstep",         // Level 14: Clubstep (Demon)
        "electrodynamix",   // Level 15: Electrodynamix
        "hexagonforce",     // Level 16: Hexagon Force
        "blastprocessing",  // Level 17: Blast Processing
        "toe2",             // Level 18: Theory of Everything 2 (Demon)
        "geometrical",      // Level 19: Geometrical Dominator
        "deadlocked",       // Level 20: Deadlocked (Demon)
        "fingerdash"        // Level 21: Fingerdash
    };

    // Load first level (Stereo Madness)
    LevelData currentLevel;
    auto loadLevel = [&](const std::string& name) -> LevelData {
        // All 21 official GD level files
        std::map<std::string, std::string> levelFiles = {
            {"stereomadness",   "G:/gd-ml-bot/levels/1.gmd"},
            {"backontrack",     "G:/gd-ml-bot/levels/2.gmd"},
            {"polargeist",      "G:/gd-ml-bot/levels/3.gmd"},
            {"dryout",          "G:/gd-ml-bot/levels/4.gmd"},
            {"baseafterbase",   "G:/gd-ml-bot/levels/5.gmd"},
            {"cantletgo",       "G:/gd-ml-bot/levels/6.gmd"},
            {"jumper",          "G:/gd-ml-bot/levels/7.gmd"},
            {"timemachine",     "G:/gd-ml-bot/levels/8.gmd"},
            {"cycles",          "G:/gd-ml-bot/levels/9.gmd"},
            {"xstep",           "G:/gd-ml-bot/levels/10.gmd"},
            {"clutterfunk",     "G:/gd-ml-bot/levels/11.gmd"},
            {"toe",             "G:/gd-ml-bot/levels/12.gmd"},
            {"electroman",      "G:/gd-ml-bot/levels/13.gmd"},
            {"clubstep",        "G:/gd-ml-bot/levels/14.gmd"},
            {"electrodynamix",  "G:/gd-ml-bot/levels/15.gmd"},
            {"hexagonforce",    "G:/gd-ml-bot/levels/16.gmd"},
            {"blastprocessing", "G:/gd-ml-bot/levels/17.gmd"},
            {"toe2",            "G:/gd-ml-bot/levels/18.gmd"},
            {"geometrical",     "G:/gd-ml-bot/levels/19.gmd"},
            {"deadlocked",      "G:/gd-ml-bot/levels/20.gmd"},
            {"fingerdash",      "G:/gd-ml-bot/levels/21.gmd"}
        };
        auto it = levelFiles.find(name);
        if (it != levelFiles.end()) {
            LevelData realLevel = LevelParser::parseFromFile(it->second);
            if (realLevel.objectCount > 0) {
                std::cout << "[REAL] Loading " << name << " from " << it->second 
                          << " (" << realLevel.objectCount << " objects)" << std::endl;
                return realLevel;
            }
            std::cerr << "[WARN] Failed to load " << it->second << std::endl;
        }
        // Fallback to generated style levels
        if (name == "stereomadness") return LevelParser::createStereoMadnessStyle();
        if (name == "backontrack") return LevelParser::createBackOnTrackStyle();
        if (name == "polargeist") return LevelParser::createPolargeistStyle();
        if (name == "dryout") return LevelParser::createDryOutStyle();
        if (name == "baseafterbase") return LevelParser::createBaseAfterBaseStyle();
        if (name == "cantletgo") return LevelParser::createCantLetGoStyle();
        if (name == "jumper") return LevelParser::createJumperStyle();
        if (name == "timemachine") return LevelParser::createTimeMachineStyle();
        if (name == "cycles") return LevelParser::createCyclesStyle();
        if (name == "xstep") return LevelParser::createXStepStyle();
        if (name == "clubstep") return LevelParser::createClubstepStyle();
        if (name == "toe2") return LevelParser::createTheoryOfEverything2Style();
        return LevelParser::createTutorialLevel();
    };

    currentLevel = loadLevel(levelOrder[currentLevelIdx]);

    Environment env;
    env.loadLevel(currentLevel);
    env.setRewards(createDefaultRewards());

    PPOAgent agent;
    agent.init(env.getObsSize(), env.getActionSize());

    // Try to load most recent checkpoint
    auto recentCheckpoint = findMostRecentCheckpoint(checkpointDir_);
    if (!recentCheckpoint.empty()) {
        agent.load(recentCheckpoint);
    }

    int episodes = 0;
    float bestProgress = 0.0f;
    auto obs = env.reset();

    std::vector<float> recentProgress;
    std::vector<float> recentRewards;
    float episodeReward = 0.0f;
    int clickCount = 0;
    int stepsSinceUpdate = 0;

    RenderState rstate;
    rstate.step = 0;
    rstate.episode = 0;

    auto startTime = std::chrono::steady_clock::now();
    auto lastCheckpointTime = startTime;

    std::cout << "[INFINITE] Starting on level: " << levelOrder[currentLevelIdx] << std::endl;
    std::cout << "[INFINITE] Target: Complete all " << levelOrder.size() << " levels!" << std::endl;
    std::cout << "[Renderer] Press ESC to exit, Q to save & quit" << std::endl;

    // Main training loop - runs forever until user stops
    while (!g_shouldStop && !renderer.shouldClose()) {
        // Poll window events (non-blocking)
        renderer.pollEvents();

        // Check for 'Q' key only in visual mode with window focused
        // Note: Only works if window is visible and focused

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
        rstate.levelName = levelOrder[currentLevelIdx];
        rstate.levelsCompleted = levelsCompleted;
        rstate.bestProgress = bestProgress;

        // Render
        renderer.renderLevel(currentLevel, rstate);
        renderer.syncFrame();

        obs = result.obs;
        episodeReward += result.reward;
        stepsSinceUpdate++;

        // Episode ended
        if (result.done) {
            episodes++;
            if (result.progress > bestProgress) {
                bestProgress = result.progress;
            }

            // Check if level completed (100% progress)
            if (result.progress >= 99.5f) {
                consecutiveCompletions++;
                // Only advance after 3 consecutive completions
                if (consecutiveCompletions >= 3) {
                    levelsCompleted++;
                    std::cout << "\n🏆 LEVEL MASTERED! " << levelOrder[currentLevelIdx] 
                              << " (" << levelsCompleted << "/" << levelOrder.size() << ") 🏆" << std::endl;
                    
                    // Advance to next level
                    currentLevelIdx++;
                    if (currentLevelIdx >= (int)levelOrder.size()) {
                        std::cout << "\n🏆 ALL LEVELS COMPLETED! 🏆" << std::endl;
                        std::cout << "Restarting from beginning with current knowledge..." << std::endl;
                        currentLevelIdx = 0;
                    }
                    consecutiveCompletions = 0;  // Reset for next level

                    // Load new level
                    currentLevel = loadLevel(levelOrder[currentLevelIdx]);
                    env.loadLevel(currentLevel);
                    env.setRewards(createDefaultRewards());

                    std::cout << "[INFINITE] Now training on: " << levelOrder[currentLevelIdx] << std::endl;
                } else {
                    std::cout << "[INFINITE] Need " << (3 - consecutiveCompletions) 
                              << " more consecutive completion(s) to advance!" << std::endl;
                }
                
                levelCompleted = false;
                bestProgress = 0.0f;
                rstate.trail.clear();
            } else {
                // Player died before completing - reset consecutive counter
                if (consecutiveCompletions > 0) {
                    std::cout << "[INFINITE] Died at " << result.progress 
                              << "% - Resetting consecutive counter (was: " 
                              << consecutiveCompletions << ")" << std::endl;
                    consecutiveCompletions = 0;
                }
            }

            recentProgress.push_back(result.progress);
            recentRewards.push_back(episodeReward);
            if (recentProgress.size() > 50) {
                recentProgress.erase(recentProgress.begin());
                recentRewards.erase(recentRewards.begin());
            }
            episodeReward = 0.0f;
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
                      << " | Level: " << levelOrder[currentLevelIdx]
                      << " | Steps: " << agent.getTotalSteps() << " =====" << std::endl;
            std::cout << "  Episodes: " << episodes
                      << " | Steps/s: " << (int)stepsPerSec
                      << " | Click%: " << (int)clickRate << "%" << std::endl;
            std::cout << "  Avg Progress: " << avgProg << "%"
                      << " | Best: " << bestProgress << "%"
                      << " | Levels Done: " << levelsCompleted << std::endl;
            std::cout << "  P.Loss: " << losses[0]
                      << " | V.Loss: " << losses[1]
                      << " | Entropy: " << losses[2] << std::endl;
            std::cout << std::endl;

            // Update render state with training stats
            rstate.avgProgress = avgProg;
            rstate.entropy = losses[2];

            clickCount = 0;
            stepsSinceUpdate = 0;

            logger_.logUpdate(agent.getTotalSteps(), agent.getUpdateCount(),
                            episodes, bestProgress,
                            losses[0], losses[1], losses[2]);

            // Regular checkpoint save every 10 updates
            if (agent.getUpdateCount() % 10 == 0) {
                agent.save(checkpointDir_ + "/latest", currentLevelIdx);
            }
        }

        // Auto-save checkpoint every 60 seconds
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastCheckpoint = std::chrono::duration<float>(now - lastCheckpointTime).count();
        if (timeSinceLastCheckpoint >= 60.0f) {
            std::string ts = getTimestampStr();
            std::cout << "[AUTO-SAVE] Saving checkpoint (ts=" << ts << ")..." << std::endl;
            agent.save(checkpointDir_ + "/latest");
            agent.save(checkpointDir_ + "/auto_" + ts);
            cleanOldAutoCheckpoints(checkpointDir_, 5);
            lastCheckpointTime = now;
        }
    }

    // Final checkpoint save on exit
    std::cout << "[TRAINER] Saving final checkpoint..." << std::endl;
    agent.save(checkpointDir_ + "/latest", currentLevelIdx);
    agent.save(checkpointDir_ + "/exit_" + std::to_string(agent.getTotalSteps()), currentLevelIdx);

    logger_.close();
    std::cout << "[TRAINER] Training stopped. Checkpoint saved." << std::endl;
    std::cout << "  Total steps: " << agent.getTotalSteps() << std::endl;
    std::cout << "  Levels completed: " << levelsCompleted << std::endl;
    std::cout << "  Final level: " << levelOrder[currentLevelIdx] << std::endl;
}

// ============================================================================
// INFINITE TRAINING WITHOUT VISUALIZER - For overnight training
// ============================================================================
void Trainer::trainInfiniteNoVisual() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  INFINITE TRAINING MODE (NO VISUAL)" << std::endl;
    std::cout << "  Complete level 3x consecutively -> advance" << std::endl;
    std::cout << "  Auto-save every 60 seconds" << std::endl;
    std::cout << "  Press Ctrl+C to stop" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Setup signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    logger_.init(logDir_);

    int currentLevelIdx = 0;
    int levelsCompleted = 0;
    int consecutiveCompletions = 0;  // Count consecutive level completions without dying

    std::vector<std::string> levelOrder = {
        "stereomadness", "backontrack", "polargeist", "dryout",
        "baseafterbase", "cantletgo", "jumper", "timemachine", "cycles",
        "xstep", "clutterfunk", "toe", "electroman", "clubstep",
        "electrodynamix", "hexagonforce", "blastprocessing", "toe2",
        "geometrical", "deadlocked", "fingerdash"
    };

    auto loadLevel = [&](const std::string& name) -> LevelData {
        // All 21 official GD level files
        std::map<std::string, std::string> levelFiles = {
            {"stereomadness",   "G:/gd-ml-bot/levels/1.gmd"},
            {"backontrack",     "G:/gd-ml-bot/levels/2.gmd"},
            {"polargeist",      "G:/gd-ml-bot/levels/3.gmd"},
            {"dryout",          "G:/gd-ml-bot/levels/4.gmd"},
            {"baseafterbase",   "G:/gd-ml-bot/levels/5.gmd"},
            {"cantletgo",       "G:/gd-ml-bot/levels/6.gmd"},
            {"jumper",          "G:/gd-ml-bot/levels/7.gmd"},
            {"timemachine",     "G:/gd-ml-bot/levels/8.gmd"},
            {"cycles",          "G:/gd-ml-bot/levels/9.gmd"},
            {"xstep",           "G:/gd-ml-bot/levels/10.gmd"},
            {"clutterfunk",     "G:/gd-ml-bot/levels/11.gmd"},
            {"toe",             "G:/gd-ml-bot/levels/12.gmd"},
            {"electroman",      "G:/gd-ml-bot/levels/13.gmd"},
            {"clubstep",        "G:/gd-ml-bot/levels/14.gmd"},
            {"electrodynamix",  "G:/gd-ml-bot/levels/15.gmd"},
            {"hexagonforce",    "G:/gd-ml-bot/levels/16.gmd"},
            {"blastprocessing", "G:/gd-ml-bot/levels/17.gmd"},
            {"toe2",            "G:/gd-ml-bot/levels/18.gmd"},
            {"geometrical",     "G:/gd-ml-bot/levels/19.gmd"},
            {"deadlocked",      "G:/gd-ml-bot/levels/20.gmd"},
            {"fingerdash",      "G:/gd-ml-bot/levels/21.gmd"}
        };
        auto it = levelFiles.find(name);
        if (it != levelFiles.end()) {
            LevelData realLevel = LevelParser::parseFromFile(it->second);
            if (realLevel.objectCount > 0) {
                std::cout << "[REAL] Loading " << name << " from " << it->second 
                          << " (" << realLevel.objectCount << " objects)" << std::endl;
                return realLevel;
            }
            std::cerr << "[WARN] Failed to load " << it->second << std::endl;
        }
        // Fallback to generated style levels
        if (name == "stereomadness") return LevelParser::createStereoMadnessStyle();
        if (name == "backontrack") return LevelParser::createBackOnTrackStyle();
        if (name == "polargeist") return LevelParser::createPolargeistStyle();
        if (name == "dryout") return LevelParser::createDryOutStyle();
        if (name == "baseafterbase") return LevelParser::createBaseAfterBaseStyle();
        if (name == "cantletgo") return LevelParser::createCantLetGoStyle();
        if (name == "jumper") return LevelParser::createJumperStyle();
        if (name == "timemachine") return LevelParser::createTimeMachineStyle();
        if (name == "cycles") return LevelParser::createCyclesStyle();
        if (name == "xstep") return LevelParser::createXStepStyle();
        if (name == "clubstep") return LevelParser::createClubstepStyle();
        if (name == "toe2") return LevelParser::createTheoryOfEverything2Style();
        return LevelParser::createStereoMadnessStyle();
    };

    LevelData currentLevel = loadLevel(levelOrder[currentLevelIdx]);

    Environment env;
    env.loadLevel(currentLevel);
    env.setRewards(createDefaultRewards());

    PPOAgent agent;
    agent.init(env.getObsSize(), env.getActionSize());

    int loadedLevelIdx = 0;
    auto recentCheckpoint = findMostRecentCheckpoint(checkpointDir_);
    if (!recentCheckpoint.empty()) {
        agent.load(recentCheckpoint, loadedLevelIdx);
        if (loadedLevelIdx >= 0 && loadedLevelIdx < (int)levelOrder.size()) {
            currentLevelIdx = loadedLevelIdx;
            currentLevel = loadLevel(levelOrder[currentLevelIdx]);
            env.loadLevel(currentLevel);
            env.setRewards(createDefaultRewards());
            std::cout << "[INFINITE] Resuming from checkpoint at level: " << levelOrder[currentLevelIdx] << std::endl;
        }
    }

    int episodes = 0;
    float bestProgress = 0.0f;
    auto obs = env.reset();

    std::vector<float> recentProgress;
    std::vector<float> recentRewards;
    float episodeReward = 0.0f;
    int clickCount = 0;
    int stepsSinceUpdate = 0;

    // External visualizer state writer
    StateWriter stateWriter;
    stateWriter.setPath("G:/gd-ml-bot/state.json");
    stateWriter.setInterval(0.08f); // ~12 fps writes, negligible overhead

    // Trail for visualizer
    std::vector<std::pair<float,float>> trail;

    // Running stats for visualizer
    float lastAvgProgress = 0.0f;
    float lastPLoss = 0.0f, lastVLoss = 0.0f, lastEntropy = 0.0f;
    int lastClickPct = 0;
    float lastStepsPerSec = 0.0f;
    int lastAction = 0;
    float lastValue = 0.0f, lastReward = 0.0f;

    auto startTime = std::chrono::steady_clock::now();
    auto lastCheckpointTime = startTime;

    std::cout << "[INFINITE] Target: Complete all " << levelOrder.size() << " levels!" << std::endl;
    std::cout << "[INFINITE] Running at maximum speed (no visualizer)" << std::endl;
    std::cout << "[VISUALIZER] State written to G:/gd-ml-bot/state.json" << std::endl;
    std::cout << "[VISUALIZER] Open watch_training.bat to view live!" << std::endl;

    while (!g_shouldStop) {
        float logProb, value;
        int action = agent.selectAction(obs, logProb, value);
        if (action == 1) clickCount++;

        auto result = env.step(action);

        // Write state for external visualizer (rate-limited, ~12fps)
        {
            const GameState& gs = result.state;
            trail.push_back({gs.playerX, gs.playerY});
            if (trail.size() > 120) trail.erase(trail.begin());
            auto nowT = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(nowT - startTime).count();
            float sps = (elapsed > 0.01f) ? (float)agent.getTotalSteps() / elapsed : 0.0f;
            stateWriter.update(
                gs.playerX, gs.playerY, result.progress,
                gs.gameMode, gs.isDead,
                gs.isOnGround, gs.gravityFlipped,
                result.reward, value,
                episodes, agent.getTotalSteps(), agent.getUpdateCount(),
                lastAvgProgress, bestProgress,
                lastPLoss, lastVLoss, lastEntropy,
                sps, lastClickPct,
                levelOrder[currentLevelIdx], levelsCompleted,
                consecutiveCompletions,
                trail
            );
        }

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
            if (result.progress > bestProgress) bestProgress = result.progress;

            if (result.progress >= 99.5f) {
                std::cout << "\n🎉 LEVEL COMPLETED! " << levelOrder[currentLevelIdx] 
                          << " (Consecutive: " << consecutiveCompletions << "/3)" << std::endl;

                // Save checkpoint immediately on completion (with level index)
                agent.save(checkpointDir_ + "/completed_" + levelOrder[currentLevelIdx], currentLevelIdx);
                agent.save(checkpointDir_ + "/latest", currentLevelIdx);

                // Only advance after 3 consecutive completions
                if (consecutiveCompletions >= 3) {
                    levelsCompleted++;
                    std::cout << "\n🏆 LEVEL MASTERED! " << levelOrder[currentLevelIdx] 
                              << " (" << levelsCompleted << "/" << levelOrder.size() << ")" << std::endl;

                    currentLevelIdx++;
                    if (currentLevelIdx >= (int)levelOrder.size()) {
                        std::cout << "\n🏆 ALL LEVELS COMPLETED! Restarting..." << std::endl;
                        currentLevelIdx = 0;
                    }
                    consecutiveCompletions = 0;  // Reset for next level

                    currentLevel = loadLevel(levelOrder[currentLevelIdx]);
                    env.loadLevel(currentLevel);
                    env.setRewards(createDefaultRewards());
                    std::cout << "[INFINITE] Now on: " << levelOrder[currentLevelIdx] << std::endl;
                } else {
                    std::cout << "[INFINITE] Need " << (3 - consecutiveCompletions) 
                              << " more consecutive completion(s) to advance!" << std::endl;
                }
                bestProgress = 0.0f;
            } else {
                // Player died before completing - reset consecutive counter
                if (consecutiveCompletions > 0) {
                    std::cout << "[INFINITE] Died at " << result.progress 
                              << "% - Resetting consecutive counter (was: " 
                              << consecutiveCompletions << ")" << std::endl;
                    consecutiveCompletions = 0;
                }
            }

            recentProgress.push_back(result.progress);
            recentRewards.push_back(episodeReward);
            if (recentProgress.size() > 50) {
                recentProgress.erase(recentProgress.begin());
                recentRewards.erase(recentRewards.begin());
            }
            episodeReward = 0.0f;
            obs = env.reset();
        }

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
                      << " | Level: " << levelOrder[currentLevelIdx]
                      << " | Steps: " << agent.getTotalSteps() << " =====" << std::endl;
            std::cout << "  Episodes: " << episodes
                      << " | Steps/s: " << (int)stepsPerSec
                      << " | Click%: " << (int)clickRate << "%" << std::endl;
            std::cout << "  Avg Progress: " << avgProg << "%"
                      << " | Best: " << bestProgress << "%"
                      << " | Levels Done: " << levelsCompleted << std::endl;
            std::cout << "  P.Loss: " << losses[0]
                      << " | V.Loss: " << losses[1]
                      << " | Entropy: " << losses[2] << std::endl;

            clickCount = 0;
            stepsSinceUpdate = 0;

            // Update cached stats for visualizer
            lastAvgProgress = avgProg;
            lastPLoss = losses[0];
            lastVLoss = losses[1];
            lastEntropy = losses[2];
            lastClickPct = (int)clickRate;
            lastStepsPerSec = stepsPerSec;

            logger_.logUpdate(agent.getTotalSteps(), agent.getUpdateCount(),
                            episodes, bestProgress,
                            losses[0], losses[1], losses[2]);

            if (agent.getUpdateCount() % 10 == 0) {
                agent.save(checkpointDir_ + "/latest", currentLevelIdx);
            }
        }

        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastCheckpoint = std::chrono::duration<float>(now - lastCheckpointTime).count();
        if (timeSinceLastCheckpoint >= 60.0f) {
            std::string ts = getTimestampStr();
            std::cout << "[AUTO-SAVE] Saving checkpoint (ts=" << ts << ")..." << std::endl;
            agent.save(checkpointDir_ + "/latest", currentLevelIdx);
            agent.save(checkpointDir_ + "/auto_" + ts, currentLevelIdx);
            cleanOldAutoCheckpoints(checkpointDir_, 5);
            lastCheckpointTime = now;
        }
    }


    std::cout << "[TRAINER] Saving final checkpoint..." << std::endl;
    agent.save(checkpointDir_ + "/latest", currentLevelIdx);
    agent.save(checkpointDir_ + "/exit_" + std::to_string(agent.getTotalSteps()), currentLevelIdx);

    logger_.close();
    std::cout << "[TRAINER] Training stopped. Checkpoint saved." << std::endl;
    std::cout << "  Total steps: " << agent.getTotalSteps() << std::endl;
    std::cout << "  Levels completed: " << levelsCompleted << std::endl;
    std::cout << "  Final level: " << levelOrder[currentLevelIdx] << std::endl;
}

#include "trainer.h"
#include "level_parser.h"
#include <iostream>
#include <string>
#include <filesystem>

// ============================================================================
// GDLearnCPP — Geometry Dash ML Bot
//
// Usage:
//   GDLearnCPP.exe train [--mode ppo|genetic|hybrid] [--steps N]
//   GDLearnCPP.exe play <checkpoint_path>
//   GDLearnCPP.exe eval <checkpoint_path>
//   GDLearnCPP.exe test
//
// Training happens in a fast internal simulator (no GD needed).
// Play mode attaches to the real GD process and plays live.
// ============================================================================

static const std::string GD_LEVELS_DIR =
    "G:/game/Geometry Dash (Build 21578706)/Resources/levels";

void printUsage() {
    std::cout << R"(
========================================
  GDLearnCPP - Geometry Dash ML Bot
========================================

Usage:
  GDLearnCPP.exe train [options]    Train the bot in simulation
  GDLearnCPP.exe play <checkpoint>  Play on real GD with trained model
  GDLearnCPP.exe eval <checkpoint>  Evaluate model on all levels
  GDLearnCPP.exe test               Quick test with generated level

Training options:
  --mode ppo|genetic|hybrid    Training algorithm (default: hybrid)
  --steps N                    Total training timesteps (default: 10000000)
  --levels tutorial|easy|medium|hard|all|N   Which levels to train on
  --visual                     Enable visual renderer during training
  --checkpoint <dir>           Checkpoint directory
  --log <dir>                  Log directory

Examples:
  GDLearnCPP.exe train --mode ppo --levels tutorial --steps 100000
  GDLearnCPP.exe train --mode hybrid --levels all --steps 50000000
  GDLearnCPP.exe train --mode genetic --levels test
  GDLearnCPP.exe play checkpoints/final/policy.bin
  GDLearnCPP.exe test
)" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "  GDLearnCPP v1.0" << std::endl;
    std::cout << "  Geometry Dash Machine Learning Bot" << std::endl;
    std::cout << "  Inspired by GigaLearnCPP for Rocket League" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    if (argc < 2) {
        printUsage();
        return 0;
    }

    std::string command = argv[1];

    Trainer trainer;
    trainer.setCheckpointDir("G:/gd-ml-bot/checkpoints");
    trainer.setLogDir("G:/gd-ml-bot/logs");

    // ========================================================================
    // TRAIN
    // ========================================================================
    if (command == "train") {
        TrainMode mode = TrainMode::HYBRID;
        int steps = ppo_config::TOTAL_TIMESTEPS;
        int pop = 500;    // genetic population
        int gen = 1000;   // genetic generations
        std::string levelArg = "test";

        // Parse arguments
        bool visualMode = false;
        bool infiniteMode = false;
        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--mode" && i + 1 < argc) {
                std::string m = argv[++i];
                if (m == "ppo") mode = TrainMode::PPO;
                else if (m == "genetic") mode = TrainMode::GENETIC;
                else mode = TrainMode::HYBRID;
            }
            else if (arg == "--steps" && i + 1 < argc) {
                steps = std::stoi(argv[++i]);
            }
            else if (arg == "--pop" && i + 1 < argc) {
                pop = std::stoi(argv[++i]);
            }
            else if (arg == "--gen" && i + 1 < argc) {
                gen = std::stoi(argv[++i]);
            }
            else if (arg == "--levels" && i + 1 < argc) {
                levelArg = argv[++i];
            }
            else if (arg == "--checkpoint" && i + 1 < argc) {
                trainer.setCheckpointDir(argv[++i]);
            }
            else if (arg == "--log" && i + 1 < argc) {
                trainer.setLogDir(argv[++i]);
            }
            else if (arg == "--visual") {
                visualMode = true;
            }
            else if (arg == "--infinite") {
                infiniteMode = true;
                steps = INT_MAX;  // Run forever
            }
        }

        trainer.setMode(mode);
        trainer.setVisualMode(visualMode);
        trainer.setInfiniteMode(infiniteMode);

        // Load levels
        if (levelArg == "all") {
            std::cout << "Loading all official GD levels..." << std::endl;
            // Official levels: 1.txt to 22.txt
            for (int i = 1; i <= 22; i++) {
                std::string path = GD_LEVELS_DIR + "/" + std::to_string(i) + ".txt";
                if (std::filesystem::exists(path)) {
                    trainer.addLevelFromFile(path);
                }
            }
            // Tower levels
            for (const auto& special : {"3001", "5001", "5002", "5003", "5004"}) {
                std::string path = GD_LEVELS_DIR + "/" + special + ".txt";
                if (std::filesystem::exists(path)) {
                    trainer.addLevelFromFile(path);
                }
            }
        }
        else if (levelArg == "test") {
            std::cout << "Using generated test levels (10 levels)..." << std::endl;
            for (int d = 1; d <= 10; d++) {
                trainer.addTestLevel(d);
            }
        }
        else if (levelArg == "easy") {
            std::cout << "Using single easy test level..." << std::endl;
            trainer.addTestLevel(1);
        }
        else if (levelArg == "tutorial") {
            std::cout << "Using tutorial level (single easy spike)..." << std::endl;
            trainer.addLevel(LevelParser::createTutorialLevel());
        }
        else if (levelArg == "medium") {
            std::cout << "Using 3 medium test levels..." << std::endl;
            for (int d = 1; d <= 3; d++) trainer.addTestLevel(d);
        }
        else if (levelArg == "hard") {
            std::cout << "Using 5 hard test levels..." << std::endl;
            for (int d = 3; d <= 7; d++) trainer.addTestLevel(d);
        }
        else if (levelArg == "superhard") {
            std::cout << "Using SUPERHARD levels (difficulty 5)..." << std::endl;
            trainer.addLevel(LevelParser::createHardLevel(5));
        }
        else if (levelArg == "real") {
            std::cout << "Loading REAL Geometry Dash official levels..." << std::endl;
            // Load all decoded .gmd levels from gd-ml-bot/levels/
            std::vector<std::string> levelFiles = {
                "G:/gd-ml-bot/levels/1.gmd",   // Stereo Madness
                "G:/gd-ml-bot/levels/2.gmd",   // Back on Track
                "G:/gd-ml-bot/levels/3.gmd",   // Polargeist
                "G:/gd-ml-bot/levels/4.gmd",   // Dry Out
                "G:/gd-ml-bot/levels/5.gmd",   // Base After Base
                "G:/gd-ml-bot/levels/6.gmd",   // Can't Let Go
                "G:/gd-ml-bot/levels/7.gmd",   // Jumper
                "G:/gd-ml-bot/levels/8.gmd",   // Time Machine
                "G:/gd-ml-bot/levels/9.gmd",   // Cycles
                "G:/gd-ml-bot/levels/10.gmd",  // xStep
            };
            for (const auto& path : levelFiles) {
                LevelData lvl = LevelParser::parseFromFile(path);
                if (lvl.objectCount > 0) {
                    trainer.addLevel(lvl);
                }
            }
            if (trainer.levelCount() == 0) {
                std::cerr << "Failed to load any real GD levels! Falling back to generated." << std::endl;
                trainer.addLevel(LevelParser::createStereoMadnessStyle());
            }
        }
        else if (levelArg == "stereomadness") {
            std::cout << "Loading REAL Stereo Madness (Level 1)..." << std::endl;
            LevelData lvl = LevelParser::parseFromFile("G:/gd-ml-bot/levels/1.gmd");
            if (lvl.objectCount > 0) trainer.addLevel(lvl);
            else trainer.addLevel(LevelParser::createStereoMadnessStyle());
        }
        else if (levelArg == "backontrack") {
            std::cout << "Loading REAL Back On Track (Level 2)..." << std::endl;
            LevelData lvl = LevelParser::parseFromFile("G:/gd-ml-bot/levels/2.gmd");
            if (lvl.objectCount > 0) trainer.addLevel(lvl);
            else trainer.addLevel(LevelParser::createBackOnTrackStyle());
        }
        else if (levelArg == "polargeist") {
            std::cout << "Loading REAL Polargeist (Level 3)..." << std::endl;
            LevelData lvl = LevelParser::parseFromFile("G:/gd-ml-bot/levels/3.gmd");
            if (lvl.objectCount > 0) trainer.addLevel(lvl);
            else trainer.addLevel(LevelParser::createPolargeistStyle());
        }
        else if (levelArg == "dryout") {
            std::cout << "Loading REAL Dry Out (Level 4)..." << std::endl;
            LevelData lvl = LevelParser::parseFromFile("G:/gd-ml-bot/levels/4.gmd");
            if (lvl.objectCount > 0) trainer.addLevel(lvl);
            else trainer.addLevel(LevelParser::createDryOutStyle());
        }
        else if (levelArg == "clubstep") {
            std::cout << "Loading REAL Clubstep (DEMON)..." << std::endl;
            LevelData lvl = LevelParser::parseFromFile("G:/gd-ml-bot/levels/14.gmd");
            if (lvl.objectCount > 0) trainer.addLevel(lvl);
            else trainer.addLevel(LevelParser::createClubstepStyle());
        }
        else if (levelArg == "toe2") {
            std::cout << "Loading REAL Theory of Everything 2 (EXTREME DEMON)..." << std::endl;
            LevelData lvl = LevelParser::parseFromFile("G:/gd-ml-bot/levels/18.gmd");
            if (lvl.objectCount > 0) trainer.addLevel(lvl);
            else trainer.addLevel(LevelParser::createTheoryOfEverything2Style());
        }
        else if (levelArg == "baseafterbase") {
            std::cout << "Using Base After Base style level (Level 5)..." << std::endl;
            trainer.addLevel(LevelParser::createBaseAfterBaseStyle());
        }
        else if (levelArg == "cantletgo") {
            std::cout << "Using Can't Let Go style level (Level 6)..." << std::endl;
            trainer.addLevel(LevelParser::createCantLetGoStyle());
        }
        else if (levelArg == "jumper") {
            std::cout << "Using Jumper style level (Level 7)..." << std::endl;
            trainer.addLevel(LevelParser::createJumperStyle());
        }
        else if (levelArg == "timemachine") {
            std::cout << "Using Time Machine style level (Level 8)..." << std::endl;
            trainer.addLevel(LevelParser::createTimeMachineStyle());
        }
        else if (levelArg == "cycles") {
            std::cout << "Using Cycles style level (Level 9)..." << std::endl;
            trainer.addLevel(LevelParser::createCyclesStyle());
        }
        else if (levelArg == "xstep") {
            std::cout << "Using xStep style level (Level 10)..." << std::endl;
            trainer.addLevel(LevelParser::createXStepStyle());
        }
        else if (levelArg == "clubstep") {
            std::cout << "Using Clubstep style level (DEMON)..." << std::endl;
            trainer.addLevel(LevelParser::createClubstepStyle());
        }
        else if (levelArg == "electrodynamix") {
            std::cout << "Using Electrodynamix style level..." << std::endl;
            trainer.addLevel(LevelParser::createElectrodynamixStyle());
        }
        else if (levelArg == "toe2") {
            std::cout << "Using Theory of Everything 2 style level (EXTREME DEMON)..." << std::endl;
            trainer.addLevel(LevelParser::createTheoryOfEverything2Style());
        }
        else if (levelArg == "ship") {
            std::cout << "Using SHIP challenge level..." << std::endl;
            trainer.addLevel(LevelParser::createShipChallenge());
        }
        else if (levelArg == "wave") {
            std::cout << "Using WAVE challenge level..." << std::endl;
            trainer.addLevel(LevelParser::createWaveChallenge());
        }
        else if (levelArg == "mixed") {
            std::cout << "Using MIXED mode level..." << std::endl;
            trainer.addLevel(LevelParser::createMixedModeLevel());
        }
        else {
            // Try to load a specific level number
            try {
                int lvl = std::stoi(levelArg);
                std::string path = GD_LEVELS_DIR + "/" + std::to_string(lvl) + ".txt";
                if (std::filesystem::exists(path)) {
                    trainer.addLevelFromFile(path);
                } else {
                    std::cerr << "Level file not found: " << path << std::endl;
                    return 1;
                }
            } catch (...) {
                // Maybe it's a file path
                if (std::filesystem::exists(levelArg)) {
                    trainer.addLevelFromFile(levelArg);
                } else {
                    std::cerr << "Invalid level argument: " << levelArg << std::endl;
                    return 1;
                }
            }
        }

        trainer.setGeneticParams(gen, pop);
        trainer.train(steps);
    }
    // ========================================================================
    // PLAY (live on real GD)
    // ========================================================================
    else if (command == "play") {
        if (argc < 3) {
            std::cerr << "Usage: GDLearnCPP.exe play <checkpoint_path>" << std::endl;
            return 1;
        }
        std::string checkpoint = argv[2];
        trainer.playLive(checkpoint);
    }
    // ========================================================================
    // EVALUATE
    // ========================================================================
    else if (command == "eval") {
        if (argc < 3) {
            std::cerr << "Usage: GDLearnCPP.exe eval <checkpoint_path>" << std::endl;
            return 1;
        }
        std::string checkpoint = argv[2];

        NeuralNet net;
        if (!net.load(checkpoint)) {
            std::cerr << "Failed to load: " << checkpoint << std::endl;
            return 1;
        }

        // Load all test levels for eval
        for (int d = 1; d <= 10; d++) {
            trainer.addTestLevel(d);
        }

        float avgProgress = trainer.evaluate(net, true);
        std::cout << "\nAverage progress across all levels: " << avgProgress << "%" << std::endl;
    }
    // ========================================================================
    // QUICK TEST
    // ========================================================================
    else if (command == "testlevels") {
        std::cout << "Testing all 21 official level files..." << std::endl;
        std::vector<std::pair<std::string, std::string>> levels = {
            {"Stereo Madness",    "G:/gd-ml-bot/levels/1.gmd"},
            {"Back On Track",     "G:/gd-ml-bot/levels/2.gmd"},
            {"Polargeist",        "G:/gd-ml-bot/levels/3.gmd"},
            {"Dry Out",           "G:/gd-ml-bot/levels/4.gmd"},
            {"Base After Base",   "G:/gd-ml-bot/levels/5.gmd"},
            {"Can't Let Go",     "G:/gd-ml-bot/levels/6.gmd"},
            {"Jumper",            "G:/gd-ml-bot/levels/7.gmd"},
            {"Time Machine",      "G:/gd-ml-bot/levels/8.gmd"},
            {"Cycles",            "G:/gd-ml-bot/levels/9.gmd"},
            {"xStep",             "G:/gd-ml-bot/levels/10.gmd"},
            {"Clutterfunk",       "G:/gd-ml-bot/levels/11.gmd"},
            {"Theory of Everything", "G:/gd-ml-bot/levels/12.gmd"},
            {"Electroman Adv.",   "G:/gd-ml-bot/levels/13.gmd"},
            {"Clubstep",          "G:/gd-ml-bot/levels/14.gmd"},
            {"Electrodynamix",    "G:/gd-ml-bot/levels/15.gmd"},
            {"Hexagon Force",     "G:/gd-ml-bot/levels/16.gmd"},
            {"Blast Processing",  "G:/gd-ml-bot/levels/17.gmd"},
            {"TOE2",              "G:/gd-ml-bot/levels/18.gmd"},
            {"Geometrical Dom.",  "G:/gd-ml-bot/levels/19.gmd"},
            {"Deadlocked",        "G:/gd-ml-bot/levels/20.gmd"},
            {"Fingerdash",        "G:/gd-ml-bot/levels/21.gmd"}
        };
        int ok = 0, fail = 0;
        for (auto& [name, path] : levels) {
            LevelData ld = LevelParser::parseFromFile(path);
            if (ld.objectCount > 0) {
                // Quick sim test: run 100 steps
                Simulator sim;
                sim.loadLevel(ld);
                int survived = 0;
                for (int s = 0; s < 100; s++) {
                    if (!sim.step(0)) break;
                    survived++;
                }
                std::cout << "  OK  " << name << " : " << ld.objectCount << " objs, "
                          << (int)ld.totalLength << " len, "
                          << ld.solids.size() << " solids, "
                          << ld.hazards.size() << " hazards, "
                          << ld.portals.size() << " portals, "
                          << ld.orbs.size() << " orbs, "
                          << ld.pads.size() << " pads, "
                          << ld.speedChanges.size() << " speeds"
                          << " | sim:" << survived << " steps" << std::endl;
                ok++;
            } else {
                std::cout << "  FAIL " << name << " : could not parse " << path << std::endl;
                fail++;
            }
        }
        std::cout << "\nResult: " << ok << " OK, " << fail << " FAILED out of " << levels.size() << std::endl;
        return fail > 0 ? 1 : 0;
    }
    else if (command == "test") {
        std::cout << "Running quick test..." << std::endl;

        // Test level parser
        auto level = LevelParser::createTestLevel(1);
        std::cout << "Test level: " << level.objectCount << " objects, "
                  << level.totalLength << " length" << std::endl;

        // Test simulator — debug physics trace
        Simulator sim;
        sim.loadLevel(level);

        // Show first spike position
        if (!level.hazards.empty()) {
            std::cout << "First spike at x=" << level.hazards[0].x
                      << " y=" << level.hazards[0].y
                      << " hitbox=" << level.hazards[0].hitboxW << "x"
                      << level.hazards[0].hitboxH << std::endl;
        }
        std::cout << "Ground Y (top): " << physics::GROUND_Y << std::endl;
        std::cout << "Player hitbox: " << 25.0f << "x" << 25.0f << std::endl;

        // Trace a jump: click for 1 frame, then release
        std::cout << "\n--- Jump trace (click frame 0, release after) ---" << std::endl;
        for (int i = 0; i < 30; i++) {
            int action = (i == 0) ? 1 : 0; // click only on first frame
            const auto& p = sim.getPlayer();
            std::cout << "  f" << i << ": x=" << (int)p.x
                      << " y=" << p.y
                      << " vy=" << p.yVelocity
                      << " ground=" << p.onGround
                      << " dead=" << p.isDead
                      << " action=" << action << std::endl;
            if (!sim.step(action)) {
                std::cout << "  DIED at frame " << i << std::endl;
                break;
            }
        }
        std::cout << "Progress after jump trace: " << sim.getProgressPercent() << "%" << std::endl;

        // Test no input
        sim.reset();
        int alive = 0;
        for (int i = 0; i < 1000; i++) {
            if (!sim.step(0)) break;
            alive++;
        }
        std::cout << "\nSim test (no input): survived " << alive << " steps, "
                  << sim.getProgressPercent() << "% progress" << std::endl;

        // Test with random inputs
        sim.reset();
        std::mt19937 rng(42);
        alive = 0;
        for (int i = 0; i < 1000; i++) {
            int action = rng() % 2;
            if (!sim.step(action)) break;
            alive++;
        }
        std::cout << "Sim test (random): survived " << alive << " steps, "
                  << sim.getProgressPercent() << "% progress" << std::endl;

        // Test environment + neural net
        Environment env;
        env.loadLevel(level);
        env.setRewards(createDefaultRewards());
        auto obs = env.reset();
        std::cout << "Observation size: " << obs.size() << std::endl;

        NeuralNet net;
        net.init({(int)obs.size(), 128, 64, 2}, 42);
        auto logits = net.forward(obs);
        auto probs = NeuralNet::softmax(logits);
        std::cout << "Net output probs: [" << probs[0] << ", " << probs[1] << "]" << std::endl;

        // Speed benchmark: how many episodes per second?
        std::cout << "\nBenchmarking simulation speed..." << std::endl;
        auto start = std::chrono::steady_clock::now();
        int totalEpisodes = 0;
        int totalSteps = 0;
        float bestProg = 0.0f;

        while (true) {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - start).count();
            if (elapsed > 5.0f) break; // 5 second benchmark

            obs = env.reset();
            for (int s = 0; s < env_config::MAX_EPISODE_STEPS; s++) {
                auto logits2 = net.forward(obs);
                auto probs2 = NeuralNet::softmax(logits2);
                int action = net.sampleAction(probs2);
                auto result = env.step(action);
                obs = result.obs;
                totalSteps++;
                if (result.done) {
                    if (result.progress > bestProg) bestProg = result.progress;
                    break;
                }
            }
            totalEpisodes++;
        }

        auto end = std::chrono::steady_clock::now();
        float secs = std::chrono::duration<float>(end - start).count();
        std::cout << "  Episodes: " << totalEpisodes << " in " << secs << "s" << std::endl;
        std::cout << "  Episodes/sec: " << (int)(totalEpisodes / secs) << std::endl;
        std::cout << "  Steps/sec: " << (int)(totalSteps / secs) << std::endl;
        std::cout << "  Best progress: " << bestProg << "%" << std::endl;

        std::cout << "\nAll tests passed!" << std::endl;
    }
    else {
        printUsage();
        return 1;
    }

    return 0;
}

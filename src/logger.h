#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <sstream>
#include <filesystem>

// ============================================================================
// Training logger — CSV metrics + console output
// ============================================================================
class Logger {
public:
    Logger() = default;

    void init(const std::string& logDir) {
        logDir_ = logDir;
        std::filesystem::create_directories(logDir);

        std::string csvPath = logDir + "/training_log.csv";
        csvFile_.open(csvPath, std::ios::app);
        if (csvFile_.is_open()) {
            // Write header if file is new/empty
            csvFile_.seekp(0, std::ios::end);
            if (csvFile_.tellp() == 0) {
                csvFile_ << "timestamp,total_steps,updates,episodes,"
                         << "avg_reward,avg_progress,best_progress,"
                         << "policy_loss,value_loss,entropy,"
                         << "episodes_per_sec,steps_per_sec" << std::endl;
            }
        }
        startTime_ = std::chrono::steady_clock::now();
        lastLogTime_ = startTime_;
        std::cout << "[Logger] Logging to " << csvPath << std::endl;
    }

    void logStep(float reward, float progress, bool done) {
        episodeRewardSum_ += reward;
        stepsSinceLog_++;
        if (done) {
            episodeRewards_.push_back(episodeRewardSum_);
            episodeProgress_.push_back(progress);
            episodeRewardSum_ = 0.0f;
            episodesSinceLog_++;
        }
    }

    void logUpdate(int totalSteps, int updates, int totalEpisodes,
                   float bestProgress, float policyLoss, float valueLoss,
                   float entropy) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastLogTime_).count();
        float totalElapsed = std::chrono::duration<float>(now - startTime_).count();

        float epsPerSec = (elapsed > 0) ? episodesSinceLog_ / elapsed : 0.0f;
        float stepsPerSec = (elapsed > 0) ? stepsSinceLog_ / elapsed : 0.0f;

        // Average metrics since last log
        float avgReward = 0.0f, avgProgress = 0.0f;
        if (!episodeRewards_.empty()) {
            for (float r : episodeRewards_) avgReward += r;
            avgReward /= episodeRewards_.size();
            for (float p : episodeProgress_) avgProgress += p;
            avgProgress /= episodeProgress_.size();
        }

        // CSV output only (console logging handled by trainer)
        if (csvFile_.is_open()) {
            csvFile_ << (int)totalElapsed << ","
                     << totalSteps << ","
                     << updates << ","
                     << totalEpisodes << ","
                     << avgReward << ","
                     << avgProgress << ","
                     << bestProgress << ","
                     << policyLoss << ","
                     << valueLoss << ","
                     << entropy << ","
                     << epsPerSec << ","
                     << (int)stepsPerSec << std::endl;
            csvFile_.flush();
        }

        // Reset counters
        episodeRewards_.clear();
        episodeProgress_.clear();
        stepsSinceLog_ = 0;
        episodesSinceLog_ = 0;
        lastLogTime_ = now;
    }

    float getElapsedSeconds() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<float>(now - startTime_).count();
    }

    void close() {
        if (csvFile_.is_open()) csvFile_.close();
    }

private:
    std::string logDir_;
    std::ofstream csvFile_;
    std::chrono::steady_clock::time_point startTime_;
    std::chrono::steady_clock::time_point lastLogTime_;

    std::vector<float> episodeRewards_;
    std::vector<float> episodeProgress_;
    float episodeRewardSum_ = 0.0f;
    int stepsSinceLog_ = 0;
    int episodesSinceLog_ = 0;
};

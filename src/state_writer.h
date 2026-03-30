#pragma once
/*
 * StateWriter - Writes training state to JSON file for external visualizer
 * Completely decoupled from training loop - zero overhead
 */
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>

class StateWriter {
public:
    StateWriter() : filePath_("G:/gd-ml-bot/state.json"), writeInterval_(0.1f) {}

    void setPath(const std::string& path) { filePath_ = path; }
    void setInterval(float seconds) { writeInterval_ = seconds; }

    // Call this every step - only writes at interval to avoid IO bottleneck
    void update(
        float playerX, float playerY, float percent,
        int gameMode, bool isDead, bool isOnGround, bool gravFlipped,
        float reward, float value,
        int episode, long long totalSteps, int updates,
        float avgProgress, float bestProgress,
        float policyLoss, float valueLoss, float entropy,
        float stepsPerSec, int clickPct,
        const std::string& levelName, int levelsCompleted,
        int consecutiveCompletions,
        const std::vector<std::pair<float,float>>& trail
    ) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastWrite_).count();
        if (elapsed < writeInterval_) return;
        lastWrite_ = now;

        writeJSON(playerX, playerY, percent, gameMode, isDead, isOnGround,
                  gravFlipped, reward, value, episode, totalSteps, updates,
                  avgProgress, bestProgress, policyLoss, valueLoss, entropy,
                  stepsPerSec, clickPct, levelName, levelsCompleted,
                  consecutiveCompletions, trail);
    }

private:
    std::string filePath_;
    float writeInterval_;
    std::chrono::steady_clock::time_point lastWrite_;

    void writeJSON(
        float px, float py, float pct,
        int gameMode, bool isDead, bool isOnGround, bool gravFlipped,
        float reward, float value,
        int episode, long long totalSteps, int updates,
        float avgProgress, float bestProgress,
        float pLoss, float vLoss, float entropy,
        float stepsPerSec, int clickPct,
        const std::string& levelName, int levelsCompleted,
        int consecutiveCompletions,
        const std::vector<std::pair<float,float>>& trail
    ) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "{\n";
        ss << "  \"px\": " << px << ",\n";
        ss << "  \"py\": " << py << ",\n";
        ss << "  \"percent\": " << pct << ",\n";
        ss << "  \"gameMode\": " << gameMode << ",\n";
        ss << "  \"isDead\": " << (isDead ? "true" : "false") << ",\n";
        ss << "  \"isOnGround\": " << (isOnGround ? "true" : "false") << ",\n";
        ss << "  \"gravFlipped\": " << (gravFlipped ? "true" : "false") << ",\n";
        ss << "  \"reward\": " << reward << ",\n";
        ss << "  \"value\": " << value << ",\n";
        ss << "  \"episode\": " << episode << ",\n";
        ss << "  \"totalSteps\": " << totalSteps << ",\n";
        ss << "  \"updates\": " << updates << ",\n";
        ss << "  \"avgProgress\": " << avgProgress << ",\n";
        ss << "  \"bestProgress\": " << bestProgress << ",\n";
        ss << "  \"policyLoss\": " << pLoss << ",\n";
        ss << "  \"valueLoss\": " << vLoss << ",\n";
        ss << "  \"entropy\": " << entropy << ",\n";
        ss << "  \"stepsPerSec\": " << stepsPerSec << ",\n";
        ss << "  \"clickPct\": " << clickPct << ",\n";
        ss << "  \"levelName\": \"" << levelName << "\",\n";
        ss << "  \"levelsCompleted\": " << levelsCompleted << ",\n";
        ss << "  \"consecutiveCompletions\": " << consecutiveCompletions << ",\n";
        ss << "  \"trail\": [";
        size_t maxTrail = std::min(trail.size(), (size_t)60);
        size_t start = trail.size() > maxTrail ? trail.size() - maxTrail : 0;
        for (size_t i = start; i < trail.size(); ++i) {
            if (i > start) ss << ",";
            ss << "[" << trail[i].first << "," << trail[i].second << "]";
        }
        ss << "]\n}\n";

        // Atomic write: write to tmp then rename
        std::string tmpPath = filePath_ + ".tmp";
        std::ofstream f(tmpPath, std::ios::trunc);
        if (f.is_open()) {
            f << ss.str();
            f.close();
            // On Windows, rename is atomic if same drive
            std::remove(filePath_.c_str());
            std::rename(tmpPath.c_str(), filePath_.c_str());
        }
    }
};

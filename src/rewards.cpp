#include "rewards.h"

// All reward implementations are in the header (inline).
// This file exists for the build system and future extensions.

RewardManager createDefaultRewards() {
    RewardManager mgr;
    // Keep shaping secondary to survival and completion so mechanics stay stable.
    mgr.addReward(std::make_unique<ProgressReward>(),       12.0f);
    mgr.addReward(std::make_unique<DeathPenalty>(),         12.0f);
    mgr.addReward(std::make_unique<SurvivalReward>(),        0.5f);
    mgr.addReward(std::make_unique<CompletionReward>(),     75.0f);
    mgr.addReward(std::make_unique<MilestoneReward>(),       2.0f);
    mgr.addReward(std::make_unique<HazardDodgeReward>(),     1.5f);
    mgr.addReward(std::make_unique<JumpTimingReward>(),      1.5f);
    mgr.addReward(std::make_unique<ProximityReward>(),       0.5f);
    mgr.addReward(std::make_unique<NoJumpWhenSafeReward>(),  0.5f);
    return mgr;
}

#include "rewards.h"

// All reward implementations are in the header (inline).
// This file exists for the build system and future extensions.

RewardManager createDefaultRewards() {
    RewardManager mgr;
    // Core rewards — simple and strong
    mgr.addReward(std::make_unique<ProgressReward>(),    10.0f);  // main signal: go forward
    mgr.addReward(std::make_unique<DeathPenalty>(),      10.0f);  // strong death penalty (-10)
    mgr.addReward(std::make_unique<SurvivalReward>(),     1.0f);  // stay alive
    mgr.addReward(std::make_unique<CompletionReward>(),  50.0f);  // huge bonus for finishing
    mgr.addReward(std::make_unique<MilestoneReward>(),    5.0f);  // bonus every 10%
    mgr.addReward(std::make_unique<HazardDodgeReward>(),  3.0f);  // reward clearing spikes
    mgr.addReward(std::make_unique<JumpTimingReward>(),   5.0f);  // reward good jumps, penalize jump-then-die
    mgr.addReward(std::make_unique<ProximityReward>(),    2.0f);  // reward approaching hazards safely
    mgr.addReward(std::make_unique<NoJumpWhenSafeReward>(), 2.0f); // reward staying calm when safe
    return mgr;
}

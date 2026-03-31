#pragma once

#include "level_parser.h"
#include <vector>
#include <cmath>
#include <algorithm>

// ============================================================================
// GD Physics Constants (matched to real game values)
// Source: community research + Cheat Engine measurements
// ============================================================================
namespace physics {

// Base speeds (blocks per second at 60fps, 1 block = 30 units)
constexpr float SPEED_HALF     = 251.16f;   // 0.5x  ~8.372 blocks/s
constexpr float SPEED_NORMAL   = 311.58f;   // 1x    ~10.386 blocks/s
constexpr float SPEED_DOUBLE   = 387.42f;   // 2x    ~12.914 blocks/s
constexpr float SPEED_TRIPLE   = 468.0f;    // 3x    ~15.6 blocks/s
constexpr float SPEED_QUAD     = 576.0f;    // 4x    ~19.2 blocks/s

// Gravity & jump (cube mode)
constexpr float GRAVITY        = -0.958199f; // per physics step (downward)
constexpr float JUMP_FORCE     = 11.18f;     // initial Y velocity on jump
constexpr float MAX_FALL_SPEED = -15.0f;     // terminal velocity

// Ship mode
constexpr float SHIP_GRAVITY      = 0.5f;   // Gravity pulls down (positive * gravDir=-1 = negative)
constexpr float SHIP_CLICK_FORCE  = -0.8f; // Click pushes up (negative * gravDir=-1 = positive)
constexpr float SHIP_MAX_Y_SPEED  = 8.0f;

// Ball mode
constexpr float BALL_GRAVITY   = 0.7f;  // Positive = pulls down

// UFO mode
constexpr float UFO_GRAVITY    = 0.7f;   // Positive = pulls down
constexpr float UFO_CLICK_FORCE = -7.0f; // Negative = pushes up

// Wave mode
constexpr float WAVE_TRAIL_SPEED = 6.0f;  // Y speed when holding/not

// Robot mode
constexpr float ROBOT_GRAVITY  = 0.9f;   // Positive = pulls down
constexpr float ROBOT_JUMP_FORCE = -12.0f; // Negative = pushes up
constexpr float ROBOT_MAX_JUMP_TIME = 0.25f; // seconds of variable jump

// Spider mode
constexpr float SPIDER_GRAVITY = 0.9f;   // Positive = pulls down

// Swing copter
constexpr float SWING_GRAVITY     = 0.7f;   // Positive = pulls down
constexpr float SWING_CLICK_FORCE = -7.0f;  // Negative = pushes up

// Simulation
constexpr float DELTA_TIME     = 1.0f / 240.0f; // physics substeps at 240Hz
constexpr int   SUBSTEPS       = 4;              // 4 substeps per frame at 60fps
constexpr float GROUND_Y       = 15.0f;          // Top of ground floor (blocks at y=0, h=30, top=15)
constexpr float CEILING_Y      = 600.0f;         // Ceiling (physical hard limit, blocks enforce real ceiling)

} // namespace physics

// ============================================================================
// Player state inside the simulator
// ============================================================================
struct SimPlayer {
    float x = 0.0f;
    float y = 0.0f;  // reset() sets correct value
    float yVelocity = 0.0f;
    float rotation = 0.0f;
    float speed = physics::SPEED_NORMAL;
    float size = 1.0f;
    bool onGround = true;
    bool isDead = false;
    bool gravityFlipped = false;
    bool isHolding = false;     // is click/space held
    int gameMode = 0;           // 0=cube,1=ship,2=ball,3=ufo,4=wave,5=robot,6=spider,7=swing
    float robotJumpTimer = 0.0f;

    // Previous frame data (for observations)
    float prevX = 0.0f;
    float prevY = 0.0f;
    float prevYVelocity = 0.0f;
};

// ============================================================================
// Simulator: runs GD physics at maximum speed (no rendering)
// This is like RocketSim for Rocket League but for Geometry Dash.
// ============================================================================
class Simulator {
public:
    Simulator();

    void loadLevel(const LevelData& level);
    void reset();

    // Step one frame (1/60s). Returns true if still alive.
    // action: 0 = release, 1 = click/hold
    bool step(int action);

    // Run multiple steps
    bool stepN(int action, int n);

    const SimPlayer& getPlayer() const { return player_; }
    const LevelData& getLevel() const { return level_; }

    float getProgressPercent() const;
    bool isCompleted() const;
    int getStepCount() const { return stepCount_; }
    float getTimeSeconds() const { return stepCount_ / 60.0f; }

    // Query spatial index: check if a cell has solid/hazard objects
    bool hasSolidAt(float x, float y, float radius) const;
    bool hasHazardAt(float x, float y, float radius) const;

private:
    SimPlayer player_;
    LevelData level_;
    int stepCount_ = 0;

    // Spatial index: objects bucketed by X region for O(1) lookup
    static constexpr float BUCKET_WIDTH = 120.0f; // 4 blocks wide
    std::vector<std::vector<const LevelObject*>> solidBuckets_;
    std::vector<std::vector<const LevelObject*>> hazardBuckets_;
    std::vector<std::vector<const LevelObject*>> orbBuckets_;
    std::vector<std::vector<const LevelObject*>> padBuckets_;
    std::vector<std::vector<const LevelObject*>> portalBuckets_;
    std::vector<std::vector<const LevelObject*>> speedBuckets_;
    int numBuckets_ = 0;

    void buildSpatialIndex();
    int getBucket(float x) const;

    void applyPhysics(float dt);
    void handleCollisions();
    bool checkAABB(float ax, float ay, float aw, float ah,
                   float bx, float by, float bw, float bh) const;

    void handleSolid(const LevelObject& obj);
    void handleHazard(const LevelObject& obj);
    void handleOrb(const LevelObject& obj);
    void handlePad(const LevelObject& obj);
    void handlePortal(const LevelObject& obj);
    void handleSpeedChange(const LevelObject& obj);
    void checkGroundCollision(float playerHeight);

    float getSpeedValue() const;
    float getPlayerHitboxW() const;
    float getPlayerHitboxH() const;
};

#include "simulator.h"
#include <iostream>
#include <cstring>

Simulator::Simulator() {}

void Simulator::loadLevel(const LevelData& level) {
    level_ = level;
    buildSpatialIndex();
    reset();
}

void Simulator::reset() {
    player_ = SimPlayer();
    player_.x = 0.0f;
    player_.y = physics::GROUND_Y + getPlayerHitboxH() * 0.5f;
    player_.yVelocity = 0.0f;
    player_.speed = physics::SPEED_NORMAL;
    player_.onGround = true;
    player_.isDead = false;
    player_.gravityFlipped = false;
    player_.isHolding = false;
    player_.gameMode = 0;
    player_.rotation = 0.0f;
    player_.robotJumpTimer = 0.0f;
    player_.prevX = 0.0f;
    player_.prevY = player_.y;
    player_.prevYVelocity = 0.0f;
    stepCount_ = 0;
    triggeredPortals_.clear(); // Clear portal triggers on reset
}

float Simulator::getProgressPercent() const {
    if (level_.totalLength <= 0.0f) return 0.0f;
    return (player_.x / level_.totalLength) * 100.0f;
}

bool Simulator::isCompleted() const {
    return getProgressPercent() >= 100.0f;
}

float Simulator::getSpeedValue() const {
    return player_.speed;
}

float Simulator::getPlayerHitboxW() const {
    // Cube hitbox: ~25x25 at scale 1.0
    float base = 25.0f;
    if (player_.gameMode == 1) base = 30.0f;  // ship is wider
    if (player_.gameMode == 4) base = 15.0f;  // wave is smaller
    return base * player_.size;
}

float Simulator::getPlayerHitboxH() const {
    float base = 25.0f;
    if (player_.gameMode == 1) base = 20.0f;  // ship is flatter
    if (player_.gameMode == 4) base = 15.0f;  // wave
    return base * player_.size;
}

// ============================================================================
// Spatial index for ultra-fast collision lookups
// ============================================================================
void Simulator::buildSpatialIndex() {
    if (level_.totalLength <= 0.0f) {
        numBuckets_ = 1;
    } else {
        numBuckets_ = (int)(level_.totalLength / BUCKET_WIDTH) + 2;
    }

    solidBuckets_.assign(numBuckets_, {});
    hazardBuckets_.assign(numBuckets_, {});
    orbBuckets_.assign(numBuckets_, {});
    padBuckets_.assign(numBuckets_, {});
    portalBuckets_.assign(numBuckets_, {});
    speedBuckets_.assign(numBuckets_, {});

    auto addToBucket = [&](std::vector<std::vector<const LevelObject*>>& buckets,
                           const LevelObject& obj) {
        int b = getBucket(obj.x);
        if (b >= 0 && b < numBuckets_) {
            buckets[b].push_back(&obj);
            // Also add to adjacent bucket for objects near boundaries
            if (b > 0) buckets[b - 1].push_back(&obj);
            if (b + 1 < numBuckets_) buckets[b + 1].push_back(&obj);
        }
    };

    for (const auto& obj : level_.solids) addToBucket(solidBuckets_, obj);
    for (const auto& obj : level_.hazards) addToBucket(hazardBuckets_, obj);
    for (const auto& obj : level_.orbs) addToBucket(orbBuckets_, obj);
    for (const auto& obj : level_.pads) addToBucket(padBuckets_, obj);
    for (const auto& obj : level_.portals) addToBucket(portalBuckets_, obj);
    for (const auto& obj : level_.speedChanges) addToBucket(speedBuckets_, obj);
}

int Simulator::getBucket(float x) const {
    int b = (int)(x / BUCKET_WIDTH);
    if (b < 0) return 0;
    if (b >= numBuckets_) return numBuckets_ - 1;
    return b;
}

// ============================================================================
// AABB collision check
// ============================================================================
bool Simulator::checkAABB(float ax, float ay, float aw, float ah,
                          float bx, float by, float bw, float bh) const {
    float aLeft   = ax - aw * 0.5f;
    float aRight  = ax + aw * 0.5f;
    float aBottom = ay - ah * 0.5f;
    float aTop    = ay + ah * 0.5f;

    float bLeft   = bx - bw * 0.5f;
    float bRight  = bx + bw * 0.5f;
    float bBottom = by - bh * 0.5f;
    float bTop    = by + bh * 0.5f;

    return aLeft < bRight && aRight > bLeft && aBottom < bTop && aTop > bBottom;
}

// ============================================================================
// Main step function — one game frame (1/60s)
// ============================================================================
bool Simulator::step(int action) {
    if (player_.isDead || isCompleted()) return false;

    // Save previous state
    player_.prevX = player_.x;
    player_.prevY = player_.y;
    player_.prevYVelocity = player_.yVelocity;

    // Set input
    bool wasHolding = player_.isHolding;
    player_.isHolding = (action == 1);

    // Single physics step per frame (1/60s)
    // GD physics constants are already tuned for per-frame application
    float frameDt = 1.0f / 60.0f;

    // Apply movement based on gamemode
    applyPhysics(frameDt);

    // Move player forward
    player_.x += player_.speed * frameDt;

    // Handle collisions
    handleCollisions();

    // Update rotation (visual, but useful for observations)
    if (player_.gameMode == 0 && !player_.onGround) {
        player_.rotation += player_.gravityFlipped ? 6.0f : -6.0f;
    } else if (player_.onGround && player_.gameMode == 0) {
        // Snap rotation to nearest 90
        player_.rotation = std::round(player_.rotation / 90.0f) * 90.0f;
    }

    // Check bounds
    if (player_.y < -100.0f || player_.y > 800.0f) {
        player_.isDead = true;
    }

    stepCount_++;
    return !player_.isDead && !isCompleted();
}

bool Simulator::stepN(int action, int n) {
    for (int i = 0; i < n; i++) {
        if (!step(action)) return false;
    }
    return true;
}

// ============================================================================
// Physics per gamemode
// ============================================================================
void Simulator::applyPhysics(float dt) {
    float gravDir = player_.gravityFlipped ? -1.0f : 1.0f;

    switch (player_.gameMode) {
    case 0: // CUBE
    {
        // Jump on click (only if on ground and just pressed)
        if (player_.isHolding && player_.onGround) {
            player_.yVelocity = physics::JUMP_FORCE * gravDir;
            player_.onGround = false;
        }
        // Gravity
        player_.yVelocity += physics::GRAVITY * gravDir;
        // Terminal velocity
        if (!player_.gravityFlipped) {
            player_.yVelocity = std::max(player_.yVelocity, physics::MAX_FALL_SPEED);
        } else {
            player_.yVelocity = std::min(player_.yVelocity, -physics::MAX_FALL_SPEED);
        }
        player_.y += player_.yVelocity;
        break;
    }
    case 1: // SHIP
    {
        if (player_.isHolding) {
            player_.yVelocity += physics::SHIP_CLICK_FORCE * gravDir;
        } else {
            player_.yVelocity += physics::SHIP_GRAVITY * gravDir;
        }
        // Clamp Y speed
        player_.yVelocity = std::clamp(player_.yVelocity,
            -physics::SHIP_MAX_Y_SPEED, physics::SHIP_MAX_Y_SPEED);
        player_.y += player_.yVelocity;
        player_.onGround = false;
        break;
    }
    case 2: // BALL
    {
        // Click toggles gravity
        if (player_.isHolding && player_.onGround) {
            player_.gravityFlipped = !player_.gravityFlipped;
            player_.onGround = false;
            gravDir = player_.gravityFlipped ? -1.0f : 1.0f;
        }
        player_.yVelocity += physics::BALL_GRAVITY * gravDir;
        player_.yVelocity = std::clamp(player_.yVelocity, physics::MAX_FALL_SPEED, -physics::MAX_FALL_SPEED);
        player_.y += player_.yVelocity;
        break;
    }
    case 3: // UFO
    {
        if (player_.isHolding) {
            player_.yVelocity = physics::UFO_CLICK_FORCE * gravDir;
        }
        player_.yVelocity += physics::UFO_GRAVITY * gravDir;
        player_.y += player_.yVelocity;
        player_.onGround = false;
        break;
    }
    case 4: // WAVE
    {
        if (player_.isHolding) {
            player_.y += physics::WAVE_TRAIL_SPEED * gravDir;
        } else {
            player_.y -= physics::WAVE_TRAIL_SPEED * gravDir;
        }
        player_.onGround = false;
        break;
    }
    case 5: // ROBOT
    {
        // Variable height jump (hold longer = jump higher)
        if (player_.isHolding && player_.onGround) {
            player_.yVelocity = physics::ROBOT_JUMP_FORCE * gravDir;
            player_.onGround = false;
            player_.robotJumpTimer = physics::ROBOT_MAX_JUMP_TIME;
        }
        if (player_.isHolding && player_.robotJumpTimer > 0.0f) {
            player_.robotJumpTimer -= dt;
            // Add extra upward force while holding
            player_.yVelocity += 0.5f * gravDir;
        } else {
            player_.robotJumpTimer = 0.0f;
        }
        player_.yVelocity += physics::ROBOT_GRAVITY * gravDir;
        player_.yVelocity = std::clamp(player_.yVelocity, physics::MAX_FALL_SPEED, -physics::MAX_FALL_SPEED);
        player_.y += player_.yVelocity;
        break;
    }
    case 6: // SPIDER
    {
        // Click teleports to nearest surface (flip gravity)
        if (player_.isHolding && player_.onGround) {
            player_.gravityFlipped = !player_.gravityFlipped;
            player_.onGround = false;
            gravDir = player_.gravityFlipped ? -1.0f : 1.0f;
            player_.yVelocity = 8.0f * gravDir;
        }
        player_.yVelocity += physics::SPIDER_GRAVITY * gravDir;
        player_.y += player_.yVelocity;
        break;
    }
    case 7: // SWING COPTER
    {
        if (player_.isHolding) {
            player_.yVelocity += physics::SWING_CLICK_FORCE * dt * 60.0f * gravDir;
        } else {
            player_.yVelocity += physics::SWING_GRAVITY * gravDir;
        }
        player_.yVelocity = std::clamp(player_.yVelocity, -10.0f, 10.0f);
        player_.y += player_.yVelocity;
        player_.onGround = false;
        break;
    }
    default:
        // Fallback to cube
        player_.yVelocity += physics::GRAVITY * gravDir;
        player_.y += player_.yVelocity;
        break;
    }
}

// ============================================================================
// Collision handling
// ============================================================================
void Simulator::handleCollisions() {
    float pw = getPlayerHitboxW();
    float ph = getPlayerHitboxH();
    int bucket = getBucket(player_.x);

    // Check bounds first
    if (bucket < 0 || bucket >= numBuckets_) {
        // Still check ground collision even out of bucket bounds
        checkGroundCollision(ph);
        return;
    }

    // =========================================================================
    // CHECK HAZARDS FIRST (before solid resolution) - death should be instant
    // =========================================================================
    for (const auto* obj : hazardBuckets_[bucket]) {
        // Use FULL hitbox for hazard detection (not reduced inner hitbox)
        // Spikes should kill even if player barely touches them
        if (checkAABB(player_.x, player_.y, pw, ph,
                     obj->x, obj->y, obj->hitboxW, obj->hitboxH)) {
            handleHazard(*obj);
            return; // Dead, no need to process other collisions
        }
    }

    // =========================================================================
    // CHECK PORTALS (before solid resolution to ensure gamemode changes)
    // Track triggered portals to avoid re-triggering while still inside
    // =========================================================================
    for (const auto* obj : portalBuckets_[bucket]) {
        // Use object's actual hitbox for portal detection
        if (checkAABB(player_.x, player_.y, pw, ph,
                     obj->x, obj->y, obj->hitboxW, obj->hitboxH)) {
            // Check if this portal was already triggered (player still inside)
            // Encode portal key as: (uint32_t)x << 32 | type
            uint64_t portalKey = (uint64_t(obj->x) << 32) | uint64_t(obj->type);
            if (triggeredPortals_.find(portalKey) != triggeredPortals_.end()) {
                continue; // Already triggered this portal, skip
            }
            
            // Mark as triggered
            triggeredPortals_.insert(portalKey);
            
            // Debug: print when portal is hit
            std::cout << "[Portal] Hit portal type=" << (int)obj->type 
                      << " id=" << obj->id << " at x=" << obj->x 
                      << " oldMode=" << player_.gameMode
                      << " -> newMode=";
            handlePortal(*obj);
            std::cout << player_.gameMode << std::endl;
        } else {
            // Player not overlapping this portal anymore, reset trigger
            uint64_t portalKey = (uint64_t(obj->x) << 32) | uint64_t(obj->type);
            triggeredPortals_.erase(portalKey);
        }
    }

    // Check speed changes
    for (const auto* obj : speedBuckets_[bucket]) {
        if (checkAABB(player_.x, player_.y, pw, ph,
                     obj->x, obj->y, obj->hitboxW, obj->hitboxH)) {
            handleSpeedChange(*obj);
        }
    }

    // =========================================================================
    // Check solids (ground/blocks) - this resolves player position
    // =========================================================================
    player_.onGround = false;
    for (const auto* obj : solidBuckets_[bucket]) {
        if (checkAABB(player_.x, player_.y, pw, ph,
                     obj->x, obj->y, obj->hitboxW, obj->hitboxH)) {
            handleSolid(*obj);
        }
    }

    // Ground floor collision (always present)
    checkGroundCollision(ph);

    // Check orbs (after solid resolution, require click)
    for (const auto* obj : orbBuckets_[bucket]) {
        if (player_.isHolding &&
            checkAABB(player_.x, player_.y, pw, ph,
                     obj->x, obj->y, obj->hitboxW, obj->hitboxH)) {
            handleOrb(*obj);
        }
    }

    // Check pads (after solid resolution, auto-trigger)
    for (const auto* obj : padBuckets_[bucket]) {
        if (checkAABB(player_.x, player_.y, pw, ph,
                     obj->x, obj->y, obj->hitboxW, obj->hitboxH)) {
            handlePad(*obj);
        }
    }
}

void Simulator::handleSolid(const LevelObject& obj) {
    float pw = getPlayerHitboxW();
    float ph = getPlayerHitboxH();

    float pBottom = player_.y - ph * 0.5f;
    float pTop    = player_.y + ph * 0.5f;
    float oBottom = obj.y - obj.hitboxH * 0.5f;
    float oTop    = obj.y + obj.hitboxH * 0.5f;
    float pLeft   = player_.x - pw * 0.5f;
    float pRight  = player_.x + pw * 0.5f;
    float oLeft   = obj.x - obj.hitboxW * 0.5f;
    float oRight  = obj.x + obj.hitboxW * 0.5f;

    // In GD, the player rides on top of ground blocks continuously.
    // We must prioritize vertical resolution over horizontal to avoid
    // false wall-death on flat ground.
    //
    // Strategy: if the player's PREVIOUS bottom was at or above the block top,
    // they are landing on it (not hitting a wall).
    float prevBottom = player_.prevY - ph * 0.5f;

    if (!player_.gravityFlipped) {
        // Check if player was above this block last frame (landing scenario)
        bool wasAbove = (prevBottom >= oTop - 2.0f); // 2 unit tolerance

        if (wasAbove || player_.yVelocity <= 0) {
            // Land on top of block
            if (pBottom < oTop && pTop > oTop) {
                player_.y = oTop + ph * 0.5f;
                player_.yVelocity = 0.0f;
                player_.onGround = true;
                return;
            }
        }

        // Check ceiling hit (player moving up into block from below)
        // In GD, hitting a block from below always kills you (ship ceiling, cube ceiling)
        bool wasBelow = (player_.prevY + ph * 0.5f <= oBottom + 2.0f);
        if (wasBelow && player_.yVelocity > 0) {
            player_.isDead = true;
            return;
        }

        // Ship/UFO/Wave: hitting any block from below = death (they can't "land" on ceilings)
        if (player_.gameMode == 1 || player_.gameMode == 3 ||
            player_.gameMode == 4 || player_.gameMode == 7) {
            // If player center is below block bottom, treat as ceiling hit = death
            if (player_.y < oBottom && pTop > oBottom) {
                player_.isDead = true;
                return;
            }
        }

        // Otherwise it's a wall collision = death
        // But only if the player is actually approaching from the side
        // (their center is between the block's top and bottom)
        float playerCenter = player_.y;
        if (playerCenter > oBottom + 3.0f && playerCenter < oTop - 3.0f) {
            player_.isDead = true;
        } else {
            // Ambiguous — resolve vertically instead of killing
            if (player_.yVelocity <= 0) {
                player_.y = oTop + ph * 0.5f;
                player_.yVelocity = 0.0f;
                player_.onGround = true;
            } else {
                player_.y = oBottom - ph * 0.5f;
                player_.yVelocity = 0.0f;
            }
        }
    } else {
        // Gravity flipped — mirror logic
        float prevTop = player_.prevY + ph * 0.5f;
        bool wasBelow = (prevTop <= oBottom + 2.0f);

        if (wasBelow || player_.yVelocity >= 0) {
            if (pTop > oBottom && pBottom < oBottom) {
                player_.y = oBottom - ph * 0.5f;
                player_.yVelocity = 0.0f;
                player_.onGround = true;
                return;
            }
        }

        bool wasAbove = (player_.prevY - ph * 0.5f >= oTop - 2.0f);
        if (wasAbove && player_.yVelocity < 0) {
            player_.y = oTop + ph * 0.5f;
            player_.yVelocity = 0.0f;
            return;
        }

        float playerCenter = player_.y;
        if (playerCenter > oBottom + 3.0f && playerCenter < oTop - 3.0f) {
            player_.isDead = true;
        } else {
            if (player_.yVelocity >= 0) {
                player_.y = oBottom - ph * 0.5f;
                player_.yVelocity = 0.0f;
                player_.onGround = true;
            } else {
                player_.y = oTop + ph * 0.5f;
                player_.yVelocity = 0.0f;
            }
        }
    }
}

void Simulator::handleHazard(const LevelObject& obj) {
    player_.isDead = true;
}

void Simulator::handleOrb(const LevelObject& obj) {
    float gravDir = player_.gravityFlipped ? -1.0f : 1.0f;
    // Yellow orb = normal jump
    if (obj.id == 36) {
        player_.yVelocity = physics::JUMP_FORCE * gravDir;
        player_.onGround = false;
    }
    // Pink orb = smaller jump
    else if (obj.id == 84) {
        player_.yVelocity = physics::JUMP_FORCE * 0.7f * gravDir;
        player_.onGround = false;
    }
    // Red orb = big jump
    else if (obj.id == 1022) {
        player_.yVelocity = physics::JUMP_FORCE * 1.4f * gravDir;
        player_.onGround = false;
    }
    // Blue orb = gravity flip
    else if (obj.id == 1330) {
        player_.gravityFlipped = !player_.gravityFlipped;
        float newGrav = player_.gravityFlipped ? -1.0f : 1.0f;
        player_.yVelocity = physics::JUMP_FORCE * newGrav;
        player_.onGround = false;
    }
    // Green orb = flip gravity + jump
    else if (obj.id == 1333) {
        player_.gravityFlipped = !player_.gravityFlipped;
        float newGrav = player_.gravityFlipped ? -1.0f : 1.0f;
        player_.yVelocity = physics::JUMP_FORCE * 0.8f * newGrav;
    }
    // Black orb = negative jump (push down)
    else if (obj.id == 1594) {
        player_.yVelocity = -physics::JUMP_FORCE * 0.8f * gravDir;
    }
    // Dash orb
    else if (obj.id == 1704 || obj.id == 1751) {
        player_.yVelocity = physics::JUMP_FORCE * 1.2f * gravDir;
        player_.onGround = false;
    }
}

void Simulator::handlePad(const LevelObject& obj) {
    float gravDir = player_.gravityFlipped ? -1.0f : 1.0f;
    // Yellow pad
    if (obj.id == 35) {
        player_.yVelocity = physics::JUMP_FORCE * 1.3f * gravDir;
        player_.onGround = false;
    }
    // Pink pad
    else if (obj.id == 67) {
        player_.yVelocity = physics::JUMP_FORCE * gravDir;
        player_.onGround = false;
    }
    // Red pad
    else if (obj.id == 140 || obj.id == 1332) {
        player_.yVelocity = physics::JUMP_FORCE * 1.6f * gravDir;
        player_.onGround = false;
    }
    // Blue pad (gravity flip)
    else if (obj.id == 1524) {
        player_.gravityFlipped = !player_.gravityFlipped;
        float newGrav = player_.gravityFlipped ? -1.0f : 1.0f;
        player_.yVelocity = physics::JUMP_FORCE * 1.3f * newGrav;
        player_.onGround = false;
    }
    // Spider pad
    else if (obj.id == 1697) {
        player_.gravityFlipped = !player_.gravityFlipped;
        player_.onGround = false;
    }
}

void Simulator::handlePortal(const LevelObject& obj) {
    switch (obj.type) {
        case ObjectType::PORTAL_GRAVITY:
            if (obj.id == 10) player_.gravityFlipped = false;       // Normal gravity
            else if (obj.id == 11) player_.gravityFlipped = true;   // Flipped
            break;
        case ObjectType::PORTAL_CUBE:    player_.gameMode = 0; break;
        case ObjectType::PORTAL_SHIP:    player_.gameMode = 1; break;
        case ObjectType::PORTAL_BALL:    player_.gameMode = 2; break;
        case ObjectType::PORTAL_UFO:     player_.gameMode = 3; break;
        case ObjectType::PORTAL_WAVE:    player_.gameMode = 4; break;
        case ObjectType::PORTAL_ROBOT:   player_.gameMode = 5; break;
        case ObjectType::PORTAL_SPIDER:  player_.gameMode = 6; break;
        case ObjectType::PORTAL_SWING:   player_.gameMode = 7; break;
        default: break;
    }
}

void Simulator::handleSpeedChange(const LevelObject& obj) {
    switch (obj.speedType) {
        case SpeedType::HALF:   player_.speed = physics::SPEED_HALF; break;
        case SpeedType::NORMAL: player_.speed = physics::SPEED_NORMAL; break;
        case SpeedType::DOUBLE: player_.speed = physics::SPEED_DOUBLE; break;
        case SpeedType::TRIPLE: player_.speed = physics::SPEED_TRIPLE; break;
        case SpeedType::QUAD:   player_.speed = physics::SPEED_QUAD; break;
    }
}

// ============================================================================
// Ground collision helper
// ============================================================================
void Simulator::checkGroundCollision(float ph) {
    if (!player_.gravityFlipped) {
        // Ground hit
        if (player_.y - ph * 0.5f <= physics::GROUND_Y) {
            player_.y = physics::GROUND_Y + ph * 0.5f;
            if (player_.yVelocity < 0) player_.yVelocity = 0.0f;
            player_.onGround = true;
        }
        // Ceiling hit — kills ship/ufo/wave/swing, kills cube too in GD
        if (player_.y + ph * 0.5f >= physics::CEILING_Y) {
            if (player_.gameMode == 1 || player_.gameMode == 3 ||
                player_.gameMode == 4 || player_.gameMode == 7) {
                // Ship/UFO/Wave/Swing: ceiling = death
                player_.isDead = true;
            } else {
                // Cube/Ball/Robot/Spider: clamp (rare edge case)
                player_.y = physics::CEILING_Y - ph * 0.5f;
                player_.yVelocity = 0.0f;
            }
        }
    } else {
        // Gravity flipped: ceiling is the floor
        if (player_.y + ph * 0.5f >= physics::CEILING_Y) {
            player_.y = physics::CEILING_Y - ph * 0.5f;
            if (player_.yVelocity > 0) player_.yVelocity = 0.0f;
            player_.onGround = true;
        }
        // Ground becomes ceiling when flipped
        if (player_.y - ph * 0.5f <= physics::GROUND_Y) {
            if (player_.gameMode == 1 || player_.gameMode == 3 ||
                player_.gameMode == 4 || player_.gameMode == 7) {
                player_.isDead = true;
            } else {
                player_.y = physics::GROUND_Y + ph * 0.5f;
                player_.yVelocity = 0.0f;
            }
        }
    }
}

// ============================================================================
// Spatial queries for environment grid scan (O(bucket) instead of O(N))
// ============================================================================
bool Simulator::hasSolidAt(float x, float y, float radius) const {
    // Ground floor
    if (y - radius <= physics::GROUND_Y) return true;

    int b = getBucket(x);
    if (b < 0 || b >= numBuckets_) return false;

    for (const auto* obj : solidBuckets_[b]) {
        if (std::abs(obj->x - x) < radius + obj->hitboxW * 0.5f &&
            std::abs(obj->y - y) < radius + obj->hitboxH * 0.5f) {
            return true;
        }
    }
    return false;
}

bool Simulator::hasHazardAt(float x, float y, float radius) const {
    int b = getBucket(x);
    if (b < 0 || b >= numBuckets_) return false;

    for (const auto* obj : hazardBuckets_[b]) {
        if (std::abs(obj->x - x) < radius + obj->hitboxW * 0.5f &&
            std::abs(obj->y - y) < radius + obj->hitboxH * 0.5f) {
            return true;
        }
    }
    return false;
}

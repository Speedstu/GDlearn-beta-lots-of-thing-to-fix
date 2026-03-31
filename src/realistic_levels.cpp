/*
 * Realistic GD Levels - Inspired by official GD levels
 * These have human-playable but challenging patterns
 */

#include "level_parser.h"
#include <iostream>
#include <cstdlib>
#include <cmath>

// Helper to add objects
// Helper to add ceiling blocks
static void addCeilingBlock(LevelData& level, float x, float y) {
    LevelObject obj;
    obj.id = 1;
    obj.x = x;
    obj.y = y;
    obj.type = ObjectType::BLOCK;
    obj.hitboxW = 30.0f;
    obj.hitboxH = 30.0f;
    level.objects.push_back(obj);
}

static void addBlock(LevelData& level, float x, float y) {
    LevelObject obj;
    obj.id = 1;
    obj.x = x;
    obj.y = y;
    obj.type = ObjectType::BLOCK;
    obj.hitboxW = 30.0f;
    obj.hitboxH = 30.0f;
    level.objects.push_back(obj);
}

static void addSpike(LevelData& level, float x, float y) {
    LevelObject obj;
    obj.id = 8;
    obj.x = x;
    obj.y = y;
    obj.type = ObjectType::SPIKE;
    obj.hitboxW = 20.0f;
    obj.hitboxH = 20.0f;
    level.objects.push_back(obj);
}

static void addOrb(LevelData& level, float x, float y, int orbType = 36) {
    LevelObject obj;
    obj.id = orbType;
    obj.x = x;
    obj.y = y;
    obj.type = ObjectType::ORB;
    obj.hitboxW = 30.0f;
    obj.hitboxH = 30.0f;
    level.objects.push_back(obj);
}

static void addPad(LevelData& level, float x, float y) {
    LevelObject obj;
    obj.id = 35;
    obj.x = x;
    obj.y = y;
    obj.type = ObjectType::PAD;
    obj.hitboxW = 30.0f;
    obj.hitboxH = 10.0f;
    level.objects.push_back(obj);
}

static void addPortal(LevelData& level, float x, float y, ObjectType type) {
    LevelObject obj;
    switch (type) {
        case ObjectType::PORTAL_SHIP: obj.id = 47; break;
        case ObjectType::PORTAL_CUBE: obj.id = 12; break;
        case ObjectType::PORTAL_BALL: obj.id = 43; break;
        case ObjectType::PORTAL_WAVE: obj.id = 660; break;
        default: obj.id = 12;
    }
    obj.x = x;
    obj.y = y;
    obj.type = type;
    obj.hitboxW = 40.0f;
    obj.hitboxH = 80.0f;
    level.objects.push_back(obj);
}

// ============================================================================
// Stereo Madness Style - Classic first level
// ============================================================================
LevelData LevelParser::createStereoMadnessStyle() {
    LevelData level;
    level.name = "StereoMadness_Style";
    const float B = 30.0f;  // block size
    
    // Ground
    for (int i = 0; i < 250; i++) {
        addBlock(level, i * B, 0);
        addBlock(level, i * B, -B);
        // Ceiling to prevent ship from flying over everything
        addCeilingBlock(level, i * B, 250.0f);
    }
    
    float x = 5 * B;
    
    // Section 1: Simple single jumps
    for (int i = 0; i < 5; i++) {
        addSpike(level, x, 35.0f);
        x += 4 * B;
    }
    
    // Section 2: Double spikes
    for (int i = 0; i < 3; i++) {
        addSpike(level, x, 35.0f);
        addSpike(level, x + B, 35.0f);
        x += 5 * B;
    }
    
    // Section 3: Triple spike (classic)
    addSpike(level, x, 35.0f);
    addSpike(level, x + B, 35.0f);
    addSpike(level, x + 2*B, 35.0f);
    x += 5 * B;
    
    // Section 4: Mixed patterns
    for (int i = 0; i < 4; i++) {
        int pattern = i % 3;
        switch (pattern) {
            case 0: // Single with orb
                addOrb(level, x, B + 15.0f);
                addSpike(level, x + 2*B, 35.0f);
                x += 5 * B;
                break;
            case 1: // Double
                addSpike(level, x, 35.0f);
                addSpike(level, x + B, 35.0f);
                x += 4 * B;
                break;
            case 2: // Single
                addSpike(level, x, 35.0f);
                x += 3 * B;
                break;
        }
    }
    
    // Section 5: Staircase with spikes
    for (int i = 0; i < 5; i++) {
        addBlock(level, x + i*B, B + i*15.0f);
        addSpike(level, x + i*B, B + i*15.0f + 35.0f);
    }
    x += 8 * B;
    
    // Section 6: Final stretch - more dense
    for (int i = 0; i < 10; i++) {
        if (i % 2 == 0) {
            addSpike(level, x, 35.0f);
        } else {
            addSpike(level, x, 35.0f);
            addSpike(level, x + B*0.7f, 35.0f);
        }
        x += 3 * B;
    }
    
    indexObjects(level);
    std::cout << "[LevelParser] Created Stereo Madness style level: " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

// ============================================================================
// Back On Track Style - Second level, slightly harder
// ============================================================================
LevelData LevelParser::createBackOnTrackStyle() {
    LevelData level;
    level.name = "BackOnTrack_Style";
    const float B = 30.0f;
    
    // Ground
    for (int i = 0; i < 200; i++) {
        addBlock(level, i * B, 0);
        addBlock(level, i * B, -B);
        // Ceiling
        addCeilingBlock(level, i * B, 250.0f);
    }
    
    float x = 4 * B;
    
    // Section 1: Intro with orbs
    for (int i = 0; i < 3; i++) {
        addOrb(level, x, B + 15.0f);
        addSpike(level, x + 2*B, 35.0f);
        x += 5 * B;
    }
    
    // Section 2: Jump pads
    for (int i = 0; i < 4; i++) {
        addPad(level, x, 0);
        addSpike(level, x + B, 35.0f);
        x += 4 * B;
    }
    
    // Section 3: Triple spikes series
    for (int i = 0; i < 5; i++) {
        addSpike(level, x, 35.0f);
        addSpike(level, x + B, 35.0f);
        addSpike(level, x + 2*B, 35.0f);
        x += 4.5f * B;
    }
    
    // Section 4: Platform section
    for (int i = 0; i < 6; i++) {
        addBlock(level, x + i*2*B, B);
        if (i % 2 == 1) {
            addSpike(level, x + i*2*B, B + 35.0f);
        }
    }
    x += 14 * B;
    
    // Section 5: Final challenge
    for (int i = 0; i < 8; i++) {
        int pat = i % 4;
        switch (pat) {
            case 0: addSpike(level, x, 35.0f); break;
            case 1: 
                addSpike(level, x, 35.0f);
                addSpike(level, x + B, 35.0f);
                break;
            case 2:
                addOrb(level, x, B + 20.0f);
                break;
            case 3:
                addPad(level, x, 0);
                break;
        }
        x += 3 * B;
    }
    
    indexObjects(level);
    std::cout << "[LevelParser] Created Back On Track style level: " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

// ============================================================================
// Polargeist Style - Third level, more challenging
// ============================================================================
LevelData LevelParser::createPolargeistStyle() {
    LevelData level;
    level.name = "Polargeist_Style";
    const float B = 30.0f;
    
    // Ground
    for (int i = 0; i < 220; i++) {
        addBlock(level, i * B, 0);
        addBlock(level, i * B, -B);
        // Ceiling to prevent ship from flying over everything
        addCeilingBlock(level, i * B, 250.0f);
    }
    
    float x = 4 * B;
    
    // More complex patterns
    srand(123);
    
    // Section 1: Alternating singles and doubles
    for (int i = 0; i < 15; i++) {
        if (i % 3 == 0) {
            addSpike(level, x, 35.0f);
        } else if (i % 3 == 1) {
            addSpike(level, x, 35.0f);
            addSpike(level, x + B, 35.0f);
        } else {
            addOrb(level, x, B + 15.0f);
        }
        x += 3.5f * B;
    }
    
    // Section 2: Ship portal section
    addPortal(level, x, 100.0f, ObjectType::PORTAL_SHIP);
    
    // Ship obstacles
    for (int i = 0; i < 50; i++) {
        addBlock(level, x + i*B, -40.0f);  // Lower ceiling
        addBlock(level, x + i*B, 200.0f);  // Ceiling
        
        if (i % 8 == 4) {
            addBlock(level, x + i*B, 80.0f);  // Obstacle
        }
    }
    x += 55 * B;
    
    // Back to cube
    addPortal(level, x, 27.5f, ObjectType::PORTAL_CUBE);
    x += 5 * B;
    
    // Section 3: Final stretch with mixed
    for (int i = 0; i < 12; i++) {
        int pat = rand() % 4;
        switch (pat) {
            case 0: addSpike(level, x, 35.0f); break;
            case 1: 
                addSpike(level, x, 35.0f);
                addSpike(level, x + B, 35.0f);
                break;
            case 2:
                addSpike(level, x, 35.0f);
                addSpike(level, x + B, 35.0f);
                addSpike(level, x + 2*B, 35.0f);
                break;
            case 3:
                addOrb(level, x, B + 20.0f);
                break;
        }
        x += 3 * B;
    }
    
    indexObjects(level);
    std::cout << "[LevelParser] Created Polargeist style level: " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

// ============================================================================
// Dry Out Style - Fourth level, harder
// ============================================================================
LevelData LevelParser::createDryOutStyle() {
    LevelData level;
    level.name = "DryOut_Style";
    const float B = 30.0f;
    
    // Ground with gaps
    float x = 0;
    for (int i = 0; i < 200; i++) {
        if (i % 15 != 7) {  // Create gaps
            addBlock(level, i * B, 0);
            addBlock(level, i * B, -B);
        }
        // Ceiling (always present even with gaps)
        addCeilingBlock(level, i * B, 250.0f);
    }
    
    x = 4 * B;
    
    // Gaps with spikes after
    for (int i = 0; i < 8; i++) {
        addSpike(level, x, 35.0f);
        addSpike(level, x + B, 35.0f);
        x += 6 * B;
    }
    
    // Triple spike section
    for (int i = 0; i < 6; i++) {
        addSpike(level, x, 35.0f);
        addSpike(level, x + B, 35.0f);
        addSpike(level, x + 2*B, 35.0f);
        x += 4 * B;
    }
    
    // Pad section
    for (int i = 0; i < 10; i++) {
        addPad(level, x, 0);
        addSpike(level, x + 1.5f*B, 35.0f);
        x += 4 * B;
    }
    
    indexObjects(level);
    std::cout << "[LevelParser] Created Dry Out style level: " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

// ============================================================================
// Base After Base Style - Fifth level
// ============================================================================
LevelData LevelParser::createBaseAfterBaseStyle() {
    LevelData level;
    level.name = "BaseAfterBase_Style";
    const float B = 30.0f;
    
    // Ground
    for (int i = 0; i < 250; i++) {
        addBlock(level, i * B, 0);
        addBlock(level, i * B, -B);
        // Ceiling to prevent ship from flying over everything
        addCeilingBlock(level, i * B, 250.0f);
    }
    
    float x = 5 * B;
    
    // Gravity flip section (simulated with upside down spikes)
    for (int i = 0; i < 20; i++) {
        int pat = i % 5;
        switch (pat) {
            case 0: case 2: case 4:
                addSpike(level, x, 35.0f);  // Normal
                break;
            case 1: case 3:
                addOrb(level, x, B + 15.0f);  // Yellow orb for flip
                break;
        }
        x += 3 * B;
    }
    
    // Ship section
    addPortal(level, x, 100.0f, ObjectType::PORTAL_SHIP);
    for (int i = 0; i < 40; i++) {
        addBlock(level, x + i*B, -50.0f);
        addBlock(level, x + i*B, 220.0f);
    }
    x += 45 * B;
    
    // Back to cube
    addPortal(level, x, 27.5f, ObjectType::PORTAL_CUBE);
    x += 5 * B;
    
    // Final stretch
    for (int i = 0; i < 15; i++) {
        addSpike(level, x, 35.0f);
        if (i % 2 == 0) {
            addSpike(level, x + B, 35.0f);
        }
        x += 3.5f * B;
    }
    
    indexObjects(level);
    std::cout << "[LevelParser] Created Base After Base style level: " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

// ============================================================================
// Cant Let Go Style - Sixth level
// ============================================================================
LevelData LevelParser::createCantLetGoStyle() {
    LevelData level;
    level.name = "CantLetGo_Style";
    const float B = 30.0f;
    
    // Ground
    for (int i = 0; i < 230; i++) {
        addBlock(level, i * B, 0);
        addBlock(level, i * B, -B);
        // Ceiling
        addCeilingBlock(level, i * B, 250.0f);
    }
    
    float x = 4 * B;
    srand(456);
    
    // Varied patterns
    for (int i = 0; i < 30; i++) {
        int pat = rand() % 5;
        switch (pat) {
            case 0: // Single
                addSpike(level, x, 35.0f);
                x += 3 * B;
                break;
            case 1: // Double
                addSpike(level, x, 35.0f);
                addSpike(level, x + B, 35.0f);
                x += 4 * B;
                break;
            case 2: // Triple
                addSpike(level, x, 35.0f);
                addSpike(level, x + B, 35.0f);
                addSpike(level, x + 2*B, 35.0f);
                x += 5 * B;
                break;
            case 3: // Orb
                addOrb(level, x, B + 15.0f);
                x += 3 * B;
                break;
            case 4: // Pad
                addPad(level, x, 0);
                x += 4 * B;
                break;
        }
    }
    
    indexObjects(level);
    std::cout << "[LevelParser] Created Can't Let Go style level: " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

// ============================================================================
// Jumper Style - Seventh level with ball
// ============================================================================
LevelData LevelParser::createJumperStyle() {
    LevelData level;
    level.name = "Jumper_Style";
    const float B = 30.0f;
    
    // Ground
    for (int i = 0; i < 240; i++) {
        addBlock(level, i * B, 0);
        addBlock(level, i * B, -B);
        // Ceiling
        addCeilingBlock(level, i * B, 250.0f);
    }
    
    float x = 4 * B;
    
    // Cube section
    for (int i = 0; i < 10; i++) {
        addSpike(level, x, 35.0f);
        x += 3.5f * B;
    }
    
    // Ball portal
    addPortal(level, x, 27.5f, ObjectType::PORTAL_BALL);
    
    // Ball section (gravity flips)
    for (int i = 0; i < 30; i++) {
        addBlock(level, x + i*B, 0);
        addBlock(level, x + i*B, -B);
        if (i % 4 == 2) {
            addSpike(level, x + i*B, 35.0f);
        }
    }
    x += 35 * B;
    
    // Back to cube
    addPortal(level, x, 27.5f, ObjectType::PORTAL_CUBE);
    x += 5 * B;
    
    // Final cube section
    for (int i = 0; i < 15; i++) {
        addSpike(level, x, 35.0f);
        if (i % 3 == 0) {
            addSpike(level, x + B, 35.0f);
        }
        x += 3.5f * B;
    }
    
    indexObjects(level);
    std::cout << "[LevelParser] Created Jumper style level: " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

// ============================================================================
// Time Machine Style - Eighth level with mirror portal
// ============================================================================
LevelData LevelParser::createTimeMachineStyle() {
    LevelData level;
    level.name = "TimeMachine_Style";
    const float B = 30.0f;
    
    // Ground
    for (int i = 0; i < 260; i++) {
        addBlock(level, i * B, 0);
        addBlock(level, i * B, -B);
        // Ceiling
        addCeilingBlock(level, i * B, 250.0f);
    }
    
    float x = 4 * B;
    srand(789);
    
    // Complex varied section
    for (int i = 0; i < 35; i++) {
        int pat = rand() % 6;
        switch (pat) {
            case 0: // Single
                addSpike(level, x, 35.0f);
                break;
            case 1: // Double
                addSpike(level, x, 35.0f);
                addSpike(level, x + B, 35.0f);
                break;
            case 2: // Triple
                addSpike(level, x, 35.0f);
                addSpike(level, x + B, 35.0f);
                addSpike(level, x + 2*B, 35.0f);
                break;
            case 3: // Orb
                addOrb(level, x, B + 15.0f);
                break;
            case 4: // Platform
                addBlock(level, x, B);
                addSpike(level, x, B + 35.0f);
                break;
            case 5: // Stacked
                addSpike(level, x, 35.0f);
                addSpike(level, x + B*0.5f, 60.0f);
                break;
        }
        x += 3 * B;
    }
    
    indexObjects(level);
    std::cout << "[LevelParser] Created Time Machine style level: " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

// ============================================================================
// Cycles Style - Ninth level
// ============================================================================
LevelData LevelParser::createCyclesStyle() {
    LevelData level;
    level.name = "Cycles_Style";
    const float B = 30.0f;
    
    // Ground
    for (int i = 0; i < 220; i++) {
        addBlock(level, i * B, 0);
        addBlock(level, i * B, -B);
        // Ceiling to prevent ship from flying over everything
        addCeilingBlock(level, i * B, 250.0f);
    }
    
    float x = 5 * B;
    
    // Ball heavy level
    addPortal(level, x, 27.5f, ObjectType::PORTAL_BALL);
    
    for (int i = 0; i < 50; i++) {
        int pat = i % 4;
        switch (pat) {
            case 0: case 2:
                addSpike(level, x + i*B, 35.0f);
                break;
            case 1: case 3:
                addSpike(level, x + i*B, 35.0f);
                addSpike(level, x + i*B, -5.0f);  // Upside down
                break;
        }
    }
    x += 55 * B;
    
    // Back to cube
    addPortal(level, x, 27.5f, ObjectType::PORTAL_CUBE);
    
    indexObjects(level);
    std::cout << "[LevelParser] Created Cycles style level: " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

// ============================================================================
// xStep Style - Tenth level
// ============================================================================
LevelData LevelParser::createXStepStyle() {
    LevelData level;
    level.name = "xStep_Style";
    const float B = 30.0f;
    
    // Ground with xStep style
    for (int i = 0; i < 280; i++) {
        addBlock(level, i * B, 0);
        addBlock(level, i * B, -B);
        // Ceiling
        addCeilingBlock(level, i * B, 250.0f);
    }
    
    float x = 4 * B;
    srand(101112);
    
    // Very varied patterns
    for (int i = 0; i < 40; i++) {
        int pat = rand() % 7;
        switch (pat) {
            case 0: // Single
                addSpike(level, x, 35.0f);
                break;
            case 1: // Double tight
                addSpike(level, x, 35.0f);
                addSpike(level, x + B*0.8f, 35.0f);
                break;
            case 2: // Triple
                addSpike(level, x, 35.0f);
                addSpike(level, x + B, 35.0f);
                addSpike(level, x + 2*B, 35.0f);
                break;
            case 3: // Orb jump
                addOrb(level, x, B + 20.0f);
                addSpike(level, x + 2*B, 35.0f);
                break;
            case 4: // Pad
                addPad(level, x, 0);
                break;
            case 5: // Staircase
                addBlock(level, x, B);
                addBlock(level, x + B, 2*B);
                break;
            case 6: // Spaced triple
                addSpike(level, x, 35.0f);
                addSpike(level, x + 1.5f*B, 35.0f);
                addSpike(level, x + 3*B, 35.0f);
                break;
        }
        x += 3 * B;
    }
    
    indexObjects(level);
    std::cout << "[LevelParser] Created xStep style level: " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

// ============================================================================
// Clubstep Style - Demon difficulty
// ============================================================================
LevelData LevelParser::createClubstepStyle() {
    LevelData level;
    level.name = "Clubstep_Style";
    const float B = 30.0f;
    
    // Ground
    for (int i = 0; i < 350; i++) {
        addBlock(level, i * B, 0);
        addBlock(level, i * B, -B);
        // Ceiling
        addCeilingBlock(level, i * B, 250.0f);
    }
    
    float x = 5 * B;
    srand(131415);
    
    // Extreme patterns
    for (int i = 0; i < 60; i++) {
        int pat = rand() % 8;
        switch (pat) {
            case 0: // Single
                addSpike(level, x, 35.0f);
                break;
            case 1: // Double tight
                addSpike(level, x, 35.0f);
                addSpike(level, x + B*0.7f, 35.0f);
                break;
            case 2: // Triple tight
                addSpike(level, x, 35.0f);
                addSpike(level, x + B*0.7f, 35.0f);
                addSpike(level, x + B*1.4f, 35.0f);
                break;
            case 3: // Quad
                addSpike(level, x, 35.0f);
                addSpike(level, x + B*0.6f, 35.0f);
                addSpike(level, x + B*1.2f, 35.0f);
                addSpike(level, x + B*1.8f, 35.0f);
                break;
            case 4: // Orb precision
                addOrb(level, x, B + 15.0f);
                addSpike(level, x + 1.5f*B, 35.0f);
                addSpike(level, x + 2.5f*B, 35.0f);
                break;
            case 5: // Platform with spike
                addBlock(level, x, B);
                addSpike(level, x, B + 35.0f);
                break;
            case 6: // Stacked double
                addSpike(level, x, 35.0f);
                addSpike(level, x, 70.0f);
                break;
            case 7: // Wave ground
                for (int j = 0; j < 5; j++) {
                    addSpike(level, x + j*B*0.5f, 35.0f + j*5.0f);
                }
                break;
        }
        x += 2.5f * B;
    }
    
    // Ship section at end
    addPortal(level, x, 100.0f, ObjectType::PORTAL_SHIP);
    for (int i = 0; i < 60; i++) {
        addBlock(level, x + i*B, -40.0f);
        addBlock(level, x + i*B, 200.0f);
    }
    
    indexObjects(level);
    std::cout << "[LevelParser] Created Clubstep style (DEMON) level: " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

// ============================================================================
// Electrodynamix Style - Speed changes
// ============================================================================
LevelData LevelParser::createElectrodynamixStyle() {
    LevelData level;
    level.name = "Electrodynamix_Style";
    const float B = 30.0f;
    
    // Ground
    for (int i = 0; i < 300; i++) {
        addBlock(level, i * B, 0);
        addBlock(level, i * B, -B);
        // Ceiling
        addCeilingBlock(level, i * B, 250.0f);
    }
    
    float x = 4 * B;
    
    // Fast paced patterns
    for (int i = 0; i < 50; i++) {
        int pat = i % 5;
        switch (pat) {
            case 0: case 2: case 4:
                addSpike(level, x, 35.0f);
                break;
            case 1:
                addSpike(level, x, 35.0f);
                addSpike(level, x + B*0.6f, 35.0f);
                break;
            case 3:
                addOrb(level, x, B + 15.0f);
                break;
        }
        x += 2.5f * B;
    }
    
    // Wave section
    addPortal(level, x, 100.0f, ObjectType::PORTAL_WAVE);
    for (int i = 0; i < 80; i++) {
        addBlock(level, x + i*B, 0);
        addBlock(level, x + i*B, 200.0f);
    }
    
    indexObjects(level);
    std::cout << "[LevelParser] Created Electrodynamix style level: " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

// ============================================================================
// Theory of Everything 2 Style - Hard demon
// ============================================================================
LevelData LevelParser::createTheoryOfEverything2Style() {
    LevelData level;
    level.name = "TheoryOfEverything2_Style";
    const float B = 30.0f;
    
    // Ground
    for (int i = 0; i < 400; i++) {
        addBlock(level, i * B, 0);
        addBlock(level, i * B, -B);
        // Ceiling
        addCeilingBlock(level, i * B, 250.0f);
    }
    
    float x = 5 * B;
    srand(161718);
    
    // Extreme demon patterns
    for (int i = 0; i < 70; i++) {
        int pat = rand() % 10;
        switch (pat) {
            case 0: // Single
            case 9:
                addSpike(level, x, 35.0f);
                break;
            case 1: // Double
                addSpike(level, x, 35.0f);
                addSpike(level, x + B*0.6f, 35.0f);
                break;
            case 2: // Triple
                addSpike(level, x, 35.0f);
                addSpike(level, x + B*0.6f, 35.0f);
                addSpike(level, x + B*1.2f, 35.0f);
                break;
            case 3: // Quad
                addSpike(level, x, 35.0f);
                addSpike(level, x + B*0.5f, 35.0f);
                addSpike(level, x + B, 35.0f);
                addSpike(level, x + B*1.5f, 35.0f);
                break;
            case 4: // Orb
                addOrb(level, x, B + 15.0f);
                break;
            case 5: // Platform
                addBlock(level, x, B);
                addSpike(level, x, B + 35.0f);
                break;
            case 6: // Stacked triple
                addSpike(level, x, 35.0f);
                addSpike(level, x, 65.0f);
                addSpike(level, x, 95.0f);
                break;
            case 7: // Wave pattern
                for (int j = 0; j < 4; j++) {
                    addSpike(level, x + j*B*0.4f, 35.0f + j*8.0f);
                }
                break;
            case 8: // Tight mini spam
                for (int j = 0; j < 6; j++) {
                    addSpike(level, x + j*B*0.3f, 35.0f);
                }
                break;
        }
        x += 2.2f * B;
    }
    
    // Wave section
    addPortal(level, x, 100.0f, ObjectType::PORTAL_WAVE);
    for (int i = 0; i < 100; i++) {
        addBlock(level, x + i*B, 0);
        addBlock(level, x + i*B, 200.0f);
    }
    
    indexObjects(level);
    std::cout << "[LevelParser] Created Theory of Everything 2 (EXTREME DEMON) level: " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

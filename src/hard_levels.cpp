/*
 * Hard level generation for superhuman training
 */

#include "level_parser.h"
#include <iostream>
#include <cstdlib>
#include <cmath>

// ============================================================================
// Hard Level - Extreme difficulty with tight spike patterns
// ============================================================================
LevelData LevelParser::createHardLevel(int difficulty) {
    LevelData level;
    level.name = "HardLevel_" + std::to_string(difficulty);
    float x = 0.0f;
    const float BLOCK = 30.0f;
    
    auto addBlock = [&](float bx, float by) {
        LevelObject obj;
        obj.id = 1;
        obj.x = bx;
        obj.y = by;
        obj.type = ObjectType::BLOCK;
        obj.hitboxW = BLOCK;
        obj.hitboxH = BLOCK;
        level.objects.push_back(obj);
    };
    
    auto addSpike = [&](float sx, float sy) {
        LevelObject obj;
        obj.id = 8;
        obj.x = sx;
        obj.y = sy;
        obj.type = ObjectType::SPIKE;
        obj.hitboxW = 20.0f;
        obj.hitboxH = 20.0f;
        level.objects.push_back(obj);
    };
    
    auto addOrb = [&](float ox, float oy) {
        LevelObject obj;
        obj.id = 36;  // Yellow orb
        obj.x = ox;
        obj.y = oy;
        obj.type = ObjectType::ORB;
        obj.hitboxW = 30.0f;
        obj.hitboxH = 30.0f;
        level.objects.push_back(obj);
    };
    
    // Build ground - longer level
    int levelBlocks = 300 + difficulty * 100;
    for (int i = 0; i < levelBlocks; i++) {
        addBlock(i * BLOCK, 0.0f);
        addBlock(i * BLOCK, -BLOCK);
    }
    
    float spikeY = 35.0f;
    srand(42 + difficulty * 100);
    
    // Hard patterns start immediately
    x = 3 * BLOCK;
    while (x < (levelBlocks - 5) * BLOCK) {
        int pattern = rand() % 10;
        
        switch (pattern) {
            case 0: // Triple spike
                addSpike(x, spikeY);
                addSpike(x + BLOCK, spikeY);
                addSpike(x + 2 * BLOCK, spikeY);
                x += 3.5f * BLOCK;
                break;
                
            case 1: // Quadruple spike
                addSpike(x, spikeY);
                addSpike(x + BLOCK, spikeY);
                addSpike(x + 2 * BLOCK, spikeY);
                addSpike(x + 3 * BLOCK, spikeY);
                x += 4.5f * BLOCK;
                break;
                
            case 2: // Spike with orb above
                addSpike(x, spikeY);
                addOrb(x, spikeY + 60.0f);
                x += 2.5f * BLOCK;
                break;
                
            case 3: // Alternating heights
                addSpike(x, spikeY);
                addSpike(x + BLOCK * 0.7f, spikeY + 15.0f);
                addSpike(x + BLOCK * 1.4f, spikeY);
                x += 2.5f * BLOCK;
                break;
                
            case 4: // Tight double
                addSpike(x, spikeY);
                addSpike(x + BLOCK * 0.5f, spikeY);
                x += 1.5f * BLOCK;
                break;
                
            case 5: // Platform jump
                addBlock(x, BLOCK);
                addSpike(x + BLOCK, BLOCK + 15.0f);
                addBlock(x + 2 * BLOCK, BLOCK);
                addSpike(x + 3 * BLOCK, BLOCK + 15.0f);
                x += 5 * BLOCK;
                break;
                
            case 6: // Stacked
                addSpike(x, spikeY);
                addSpike(x + BLOCK * 0.3f, spikeY + 25.0f);
                x += 1.5f * BLOCK;
                break;
                
            case 7: // Wave-like ground spikes
                for (int i = 0; i < 5; i++) {
                    addSpike(x + i * BLOCK * 0.6f, spikeY + i * 5.0f);
                }
                x += 4 * BLOCK;
                break;
                
            case 8: // Precision timing
                addSpike(x, spikeY);
                addOrb(x + BLOCK, spikeY + 40.0f);
                addSpike(x + 2 * BLOCK, spikeY);
                x += 3 * BLOCK;
                break;
                
            case 9: // Mini spam
                for (int i = 0; i < 6; i++) {
                    addSpike(x + i * BLOCK * 0.4f, spikeY);
                }
                x += 3 * BLOCK;
                break;
        }
    }
    
    indexObjects(level);
    std::cout << "[LevelParser] Created HARD level '" << level.name
              << "' with " << level.objectCount << " objects, length="
              << level.totalLength << std::endl;
    return level;
}

// ============================================================================
// Ship Challenge - Flying section with tight corridors
// ============================================================================
LevelData LevelParser::createShipChallenge() {
    LevelData level;
    level.name = "ShipChallenge";
    const float BLOCK = 30.0f;
    float x = 0.0f;
    
    auto addBlock = [&](float bx, float by) {
        LevelObject obj;
        obj.id = 1;
        obj.x = bx;
        obj.y = by;
        obj.type = ObjectType::BLOCK;
        obj.hitboxW = BLOCK;
        obj.hitboxH = BLOCK;
        level.objects.push_back(obj);
    };
    
    auto addSpike = [&](float sx, float sy) {
        LevelObject obj;
        obj.id = 8;
        obj.x = sx;
        obj.y = sy;
        obj.type = ObjectType::SPIKE;
        obj.hitboxW = 20.0f;
        obj.hitboxH = 20.0f;
        level.objects.push_back(obj);
    };
    
    // Ship portal at start
    LevelObject shipPortal;
    shipPortal.id = 47;  // Ship portal
    shipPortal.x = 60.0f;
    shipPortal.y = 100.0f;
    shipPortal.type = ObjectType::PORTAL_SHIP;
    level.objects.push_back(shipPortal);
    
    // Ceiling and floor for ship section
    for (int i = 0; i < 400; i++) {
        addBlock(i * BLOCK, -50.0f);  // Floor
        addBlock(i * BLOCK, 250.0f);  // Ceiling
    }
    
    srand(12345);
    x = 5 * BLOCK;
    
    // Ship obstacles
    while (x < 380 * BLOCK) {
        int pattern = rand() % 6;
        
        switch (pattern) {
            case 0: // Upward corridor
                for (int i = 0; i < 8; i++) {
                    addBlock(x + i * BLOCK * 0.5f, 150.0f + i * 10.0f);
                }
                x += 6 * BLOCK;
                break;
                
            case 1: // Downward corridor
                for (int i = 0; i < 8; i++) {
                    addBlock(x + i * BLOCK * 0.5f, 50.0f - i * 10.0f);
                }
                x += 6 * BLOCK;
                break;
                
            case 2: // Spikes on floor
                for (int i = 0; i < 5; i++) {
                    addSpike(x + i * BLOCK, 35.0f);
                }
                x += 6 * BLOCK;
                break;
                
            case 3: // Narrow passage
                addBlock(x, 80.0f);
                addBlock(x + BLOCK, 80.0f);
                addBlock(x, 120.0f);
                addBlock(x + BLOCK, 120.0f);
                x += 3 * BLOCK;
                break;
                
            case 4: // Wave pattern
                for (int i = 0; i < 15; i++) {
                    float y = 100.0f + sin(i * 0.5f) * 50.0f;
                    addBlock(x + i * BLOCK * 0.4f, y);
                }
                x += 7 * BLOCK;
                break;
                
            case 5: // Precision gates
                addBlock(x, 60.0f);
                addBlock(x, 140.0f);
                addBlock(x + BLOCK * 0.3f, 60.0f);
                addBlock(x + BLOCK * 0.3f, 140.0f);
                x += 2 * BLOCK;
                break;
        }
    }
    
    // Cube portal at end
    LevelObject cubePortal;
    cubePortal.id = 12;  // Cube portal
    cubePortal.x = 390 * BLOCK;
    cubePortal.y = 100.0f;
    cubePortal.type = ObjectType::PORTAL_CUBE;
    level.objects.push_back(cubePortal);
    
    indexObjects(level);
    std::cout << "[LevelParser] Created SHIP challenge with " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

// ============================================================================
// Wave Challenge - Tight wave sections
// ============================================================================
LevelData LevelParser::createWaveChallenge() {
    LevelData level;
    level.name = "WaveChallenge";
    const float BLOCK = 30.0f;
    float x = 0.0f;
    
    auto addBlock = [&](float bx, float by) {
        LevelObject obj;
        obj.id = 1;
        obj.x = bx;
        obj.y = by;
        obj.type = ObjectType::BLOCK;
        obj.hitboxW = BLOCK;
        obj.hitboxH = BLOCK;
        level.objects.push_back(obj);
    };
    
    // Wave portal
    LevelObject wavePortal;
    wavePortal.id = 660;  // Wave portal
    wavePortal.x = 60.0f;
    wavePortal.y = 100.0f;
    wavePortal.type = ObjectType::PORTAL_WAVE;
    level.objects.push_back(wavePortal);
    
    // Wave corridor
    for (int i = 0; i < 500; i++) {
        addBlock(i * BLOCK, 0.0f);      // Bottom
        addBlock(i * BLOCK, 200.0f);    // Top
    }
    
    srand(54321);
    x = 5 * BLOCK;
    
    while (x < 480 * BLOCK) {
        int pattern = rand() % 5;
        
        switch (pattern) {
            case 0: // Zigzag
                for (int i = 0; i < 10; i++) {
                    float y = 20.0f + (i % 2) * 160.0f;
                    addBlock(x + i * BLOCK * 0.3f, y);
                }
                x += 4 * BLOCK;
                break;
                
            case 1: // Narrow section
                for (int i = 0; i < 15; i++) {
                    addBlock(x + i * BLOCK * 0.2f, 80.0f);
                    addBlock(x + i * BLOCK * 0.2f, 120.0f);
                }
                x += 4 * BLOCK;
                break;
                
            case 2: // Spikes
                for (int i = 0; i < 8; i++) {
                    LevelObject spike;
                    spike.id = 8;
                    spike.x = x + i * BLOCK * 0.4f;
                    spike.y = (i % 2 == 0) ? 35.0f : 165.0f;
                    spike.type = ObjectType::SPIKE;
                    spike.hitboxW = 20.0f;
                    spike.hitboxH = 20.0f;
                    level.objects.push_back(spike);
                }
                x += 4 * BLOCK;
                break;
                
            case 3: // Wavy ground
                for (int i = 0; i < 20; i++) {
                    float y = sin(i * 0.4f) * 30.0f + 50.0f;
                    addBlock(x + i * BLOCK * 0.3f, y);
                }
                x += 7 * BLOCK;
                break;
                
            case 4: // Dual path
                for (int i = 0; i < 12; i++) {
                    if (i % 3 != 0) {
                        addBlock(x + i * BLOCK * 0.25f, 50.0f);
                        addBlock(x + i * BLOCK * 0.25f, 150.0f);
                    }
                }
                x += 4 * BLOCK;
                break;
        }
    }
    
    indexObjects(level);
    std::cout << "[LevelParser] Created WAVE challenge with " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

// ============================================================================
// Mixed Mode - All gamemodes
// ============================================================================
LevelData LevelParser::createMixedModeLevel() {
    LevelData level;
    level.name = "MixedMode";
    const float BLOCK = 30.0f;
    
    auto addBlock = [&](float bx, float by) {
        LevelObject obj;
        obj.id = 1;
        obj.x = bx;
        obj.y = by;
        obj.type = ObjectType::BLOCK;
        obj.hitboxW = BLOCK;
        obj.hitboxH = BLOCK;
        level.objects.push_back(obj);
    };
    
    auto addSpike = [&](float sx, float sy) {
        LevelObject obj;
        obj.id = 8;
        obj.x = sx;
        obj.y = sy;
        obj.type = ObjectType::SPIKE;
        obj.hitboxW = 20.0f;
        obj.hitboxH = 20.0f;
        level.objects.push_back(obj);
    };
    
    // Section 1: Cube with spikes
    for (int i = 0; i < 100; i++) {
        addBlock(i * BLOCK, 0.0f);
        addBlock(i * BLOCK, -BLOCK);
    }
    
    float x = 5 * BLOCK;
    while (x < 80 * BLOCK) {
        addSpike(x, 35.0f);
        x += 2.5f * BLOCK;
    }
    
    // Ship portal
    LevelObject shipPortal;
    shipPortal.id = 47;
    shipPortal.x = 90 * BLOCK;
    shipPortal.y = 100.0f;
    shipPortal.type = ObjectType::PORTAL_SHIP;
    level.objects.push_back(shipPortal);
    
    // Ship section
    for (int i = 100; i < 200; i++) {
        addBlock(i * BLOCK, -50.0f);
        addBlock(i * BLOCK, 250.0f);
    }
    
    // Wave portal
    LevelObject wavePortal;
    wavePortal.id = 660;
    wavePortal.x = 210 * BLOCK;
    wavePortal.y = 100.0f;
    wavePortal.type = ObjectType::PORTAL_WAVE;
    level.objects.push_back(wavePortal);
    
    // Wave section
    for (int i = 220; i < 320; i++) {
        addBlock(i * BLOCK, 0.0f);
        addBlock(i * BLOCK, 200.0f);
    }
    
    // Back to cube
    LevelObject cubePortal;
    cubePortal.id = 12;
    cubePortal.x = 330 * BLOCK;
    cubePortal.y = 27.5f;
    cubePortal.type = ObjectType::PORTAL_CUBE;
    level.objects.push_back(cubePortal);
    
    // Final cube section
    for (int i = 340; i < 400; i++) {
        addBlock(i * BLOCK, 0.0f);
        addBlock(i * BLOCK, -BLOCK);
    }
    
    x = 350 * BLOCK;
    while (x < 390 * BLOCK) {
        addSpike(x, 35.0f);
        x += 1.8f * BLOCK;
    }
    
    indexObjects(level);
    std::cout << "[LevelParser] Created MIXED MODE level with " 
              << level.objectCount << " objects" << std::endl;
    return level;
}

// ============================================================================
// Load from GD Game Directory
// ============================================================================
LevelData LevelParser::loadFromGDGame(const std::string& levelName) {
    // Common GD paths
    std::vector<std::string> possiblePaths = {
        "C:/Program Files (x86)/Steam/steamapps/common/Geometry Dash/Resources/levels/" + levelName + ".gmd",
        "C:/Program Files/Steam/steamapps/common/Geometry Dash/Resources/levels/" + levelName + ".gmd",
        "G:/game/Geometry Dash (Build 21578706)/Resources/levels/" + levelName + ".gmd",
        "./levels/" + levelName + ".gmd",
        "./" + levelName + ".gmd"
    };
    
    for (const auto& path : possiblePaths) {
        LevelData level = parseFromFile(path);
        if (level.objectCount > 0) {
            std::cout << "[LevelParser] Loaded real GD level: " << levelName 
                      << " from " << path << std::endl;
            return level;
        }
    }
    
    std::cerr << "[LevelParser] Could not find GD level: " << levelName 
              << ", falling back to generated level" << std::endl;
    return createHardLevel(5);
}

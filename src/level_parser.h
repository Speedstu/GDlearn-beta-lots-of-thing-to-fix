#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <map>

// ============================================================================
// GD Level Object representation
// ============================================================================

enum class ObjectType {
    BLOCK,          // Solid ground/platform
    SPIKE,          // Triangle spike (death)
    ORB,            // Jump orb (click while touching)
    PAD,            // Jump pad (auto-triggers)
    PORTAL_GRAVITY, // Flips gravity
    PORTAL_SHIP,    // Ship gamemode
    PORTAL_BALL,    // Ball gamemode
    PORTAL_UFO,     // UFO gamemode
    PORTAL_WAVE,    // Wave gamemode
    PORTAL_ROBOT,   // Robot gamemode
    PORTAL_SPIDER,  // Spider gamemode
    PORTAL_CUBE,    // Back to cube
    PORTAL_SWING,   // Swing copter
    PORTAL_SPEED,   // Speed change
    DECORATION,     // Non-gameplay (ignored by sim)
    UNKNOWN
};

enum class SpeedType {
    HALF    = 0,   // 0.5x
    NORMAL  = 1,   // 1x
    DOUBLE  = 2,   // 2x
    TRIPLE  = 3,   // 3x
    QUAD    = 4,   // 4x
};

struct LevelObject {
    int id = 0;                     // GD object ID
    float x = 0.0f;                 // X position
    float y = 0.0f;                 // Y position
    float rotation = 0.0f;          // Rotation degrees
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    bool flipX = false;
    bool flipY = false;
    int groupID = 0;
    ObjectType type = ObjectType::UNKNOWN;
    SpeedType speedType = SpeedType::NORMAL;

    // Collision box (computed from ID)
    float hitboxW = 30.0f;          // Width
    float hitboxH = 30.0f;          // Height
};

struct LevelData {
    std::string name;
    std::vector<LevelObject> objects;
    float totalLength = 0.0f;       // Computed from rightmost object
    int objectCount = 0;

    // Sorted/indexed for fast collision
    std::vector<LevelObject> solids;     // Blocks
    std::vector<LevelObject> hazards;    // Spikes
    std::vector<LevelObject> orbs;
    std::vector<LevelObject> pads;
    std::vector<LevelObject> portals;
    std::vector<LevelObject> speedChanges;
};

class LevelParser {
public:
    // Parse from file (Resources/levels/X.txt)
    static LevelData parseFromFile(const std::string& filepath);

    // Parse from raw GD level string (already decompressed)
    static LevelData parseFromString(const std::string& levelString);

    // Decode official level (base64 + gzip)
    static std::string decodeOfficialLevel(const std::string& encoded);

    // Create a simple test level programmatically
    static LevelData createTestLevel(int difficulty = 1);

    // Add ceiling blocks to any level (prevents ship from flying over)
    static void addCeilingToLevel(LevelData& level, float ceilingY = 250.0f);
    
    static LevelObject createCeilingBlock(float x, float y);

    // Create tutorial level with single easy spike
    static LevelData createTutorialLevel();
    
    // Create hard levels for superhuman training
    static LevelData createHardLevel(int difficulty = 1);      // Difficulty 1-5
    static LevelData createShipChallenge();                     // Ship flying section
    static LevelData createWaveChallenge();                     // Wave section
    static LevelData createMixedModeLevel();                    // All gamemodes
    
    // Create realistic GD-style levels (inspired by official levels)
    static LevelData createStereoMadnessStyle();                // Level 1 style
    static LevelData createBackOnTrackStyle();                  // Level 2 style
    static LevelData createPolargeistStyle();                   // Level 3 style
    static LevelData createDryOutStyle();                       // Level 4 style
    static LevelData createBaseAfterBaseStyle();                // Level 5 style
    static LevelData createCantLetGoStyle();                    // Level 6 style
    static LevelData createJumperStyle();                       // Level 7 style
    static LevelData createTimeMachineStyle();                  // Level 8 style
    static LevelData createCyclesStyle();                       // Level 9 style
    static LevelData createXStepStyle();                        // Level 10 style
    static LevelData createClubstepStyle();                     // Level 14 style (Demon)
    static LevelData createElectrodynamixStyle();               // Level 15 style
    static LevelData createTheoryOfEverything2Style();          // Level 18 style (Extreme Demon)
    
    // Load from GD game directory
    static LevelData loadFromGDGame(const std::string& levelName);

private:
    static ObjectType classifyObject(int id);
    static void computeHitbox(LevelObject& obj);
    static void indexObjects(LevelData& level);
    static std::map<int, std::string> parseObjectProperties(const std::string& objStr);
};

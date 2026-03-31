#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "level_parser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cmath>

#ifdef _WIN32
#include <Windows.h>
#include <compressapi.h>
#pragma comment(lib, "Cabinet.lib")
#pragma comment(lib, "Rpcrt4.lib")

// Missing constant definition
#ifndef COMPRESS_ALGORITHM_RAW
#define COMPRESS_ALGORITHM_RAW 3
#endif
#endif

// ============================================================================
// Minimal base64 decoder
// ============================================================================
static const std::string B64_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64UrlToStandard(const std::string& input) {
    std::string out = input;
    for (auto& c : out) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (out.size() % 4 != 0) out += '=';
    return out;
}

static std::vector<uint8_t> base64Decode(const std::string& encoded) {
    std::string in = base64UrlToStandard(encoded);
    std::vector<uint8_t> out;
    int val = 0, bits = -8;
    for (char c : in) {
        if (c == '=') break;
        size_t pos = B64_CHARS.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + (int)pos;
        bits += 6;
        if (bits >= 0) {
            out.push_back((uint8_t)((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

// ============================================================================
// Minimal gzip/zlib decompressor using WinAPI (no external deps)
// We use a simple approach: try raw deflate, then zlib, then gzip
// ============================================================================
#ifdef _WIN32
#include <Windows.h>
#include <compressapi.h>
#pragma comment(lib, "Cabinet.lib")
#pragma comment(lib, "Rpcrt4.lib")

// Simple gzip decompress using Windows API
static std::string decompressGzip(const std::vector<uint8_t>& data) {
    // Check gzip magic
    if (data.size() < 10 || data[0] != 0x1f || data[1] != 0x8b) {
        return "";
    }
    
    // Parse gzip header
    size_t pos = 10; // Skip gzip header (10 bytes)
    uint8_t flg = data[3];
    
    if (flg & 0x04) { // FEXTRA
        if (pos + 2 > data.size()) return "";
        uint16_t xlen = data[pos] | (data[pos + 1] << 8);
        pos += 2 + xlen;
    }
    if (flg & 0x08) { // FNAME - skip null-terminated string
        while (pos < data.size() && data[pos] != 0) pos++;
        pos++;
    }
    if (flg & 0x10) { // FCOMMENT - skip null-terminated string  
        while (pos < data.size() && data[pos] != 0) pos++;
        pos++;
    }
    if (flg & 0x02) pos += 2; // FHCRC
    
    if (pos >= data.size()) return "";
    
    // Get uncompressed size from gzip footer (last 4 bytes)
    uint32_t uncompressedSize = 0;
    if (data.size() >= 4) {
        size_t footerPos = data.size() - 4;
        uncompressedSize = data[footerPos] 
                        | (data[footerPos + 1] << 8)
                        | (data[footerPos + 2] << 16) 
                        | (data[footerPos + 3] << 24);
    }
    
    // Allocate output buffer
    if (uncompressedSize == 0 || uncompressedSize > 50 * 1024 * 1024) {
        uncompressedSize = 10 * 1024 * 1024; // 10MB default
    }
    
    // Create decompressor
    DECOMPRESSOR_HANDLE decompressor = NULL;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_MSZIP, NULL, &decompressor)) {
        // Try with raw deflate algorithm
        if (!CreateDecompressor(COMPRESS_ALGORITHM_RAW, NULL, &decompressor)) {
            return "";
        }
    }
    
    // Get raw deflate data (exclude gzip header and footer)
    size_t compressedSize = data.size() - pos - 8;
    if (compressedSize == 0) {
        CloseDecompressor(decompressor);
        return "";
    }
    
    // Try to decompress
    std::vector<uint8_t> output(uncompressedSize + 1024 * 1024); // Extra 1MB buffer
    SIZE_T actualSize = 0;
    
    BOOL result = Decompress(
        decompressor,
        (const void*)(data.data() + pos),
        compressedSize,
        output.data(),
        output.size(),
        &actualSize
    );
    
    CloseDecompressor(decompressor);
    
    if (result && actualSize > 0) {
        return std::string((char*)output.data(), actualSize);
    }
    
    return "";
}
#else
static std::string decompressGzip(const std::vector<uint8_t>& data) {
    return "";
}
#endif

// ============================================================================
// Level string parsing
// ============================================================================

std::string LevelParser::decodeOfficialLevel(const std::string& encoded) {
    std::string fullB64 = encoded;
    // Official levels need the header prepended
    if (fullB64.substr(0, 4) != "H4sI") {
        fullB64 = "H4sIAAAAAAAAA" + fullB64;
    }
    auto compressed = base64Decode(fullB64);
    return decompressGzip(compressed);
}

std::map<int, std::string> LevelParser::parseObjectProperties(const std::string& objStr) {
    std::map<int, std::string> props;
    std::stringstream ss(objStr);
    std::string token;
    std::vector<std::string> parts;

    while (std::getline(ss, token, ',')) {
        parts.push_back(token);
    }

    // Properties are key,value pairs
    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        try {
            int key = std::stoi(parts[i]);
            props[key] = parts[i + 1];
        } catch (...) {}
    }
    return props;
}

ObjectType LevelParser::classifyObject(int id) {
    // IMPORTANT: check portals/orbs/pads FIRST to avoid ID conflicts
    // with block/spike ranges (e.g. 660 is wave portal, NOT a block)

    // Gravity portals
    if (id == 10 || id == 11) return ObjectType::PORTAL_GRAVITY;

    // Gamemode portals
    if (id == 13) return ObjectType::PORTAL_SHIP;    // Ship portal (blue)
    if (id == 12) return ObjectType::PORTAL_CUBE;    // Cube portal (orange)
    if (id == 47 || id == 111) return ObjectType::PORTAL_SHIP;  // Alternate ship
    if (id == 46) return ObjectType::PORTAL_BALL;
    if (id == 747) return ObjectType::PORTAL_UFO;
    if (id == 660 || id == 1049) return ObjectType::PORTAL_WAVE;
    if (id == 745) return ObjectType::PORTAL_ROBOT;
    if (id == 1331) return ObjectType::PORTAL_SPIDER;
    if (id == 1933) return ObjectType::PORTAL_SWING;

    // Speed portals
    if (id == 200 || id == 201 || id == 202 || id == 203 || id == 1334)
        return ObjectType::PORTAL_SPEED;

    // Jump orbs (checked before spikes — 141 is an orb, not a spike)
    if (id == 36 || id == 84 || id == 141 || id == 1022 ||
        id == 1330 || id == 1333 || id == 1594 || id == 1704 ||
        id == 1751)
        return ObjectType::ORB;

    // Jump pads (checked before spikes — 140 is a red pad, not a spike)
    if (id == 35 || id == 67 || id == 140 || id == 1332 ||
        id == 1524 || id == 1697)
        return ObjectType::PAD;

    // Spikes (hazards) — removed 140 (red pad) and 141 (orb)
    if (id == 8 || id == 9 || id == 39 || id == 61 || id == 103 ||
        id == 135 || id == 143 ||
        id == 205 || id == 363 || id == 364 || id == 365 ||
        id == 392 || id == 393 || id == 394 || id == 446 ||
        id == 447 || id == 667 || id == 720 || id == 721 ||
        id == 722 || id == 768 || id == 769 || id == 989 ||
        id == 991)
        return ObjectType::SPIKE;

    // Blocks (solid) — removed 660-695 range overlap with wave portal
    if ((id >= 1 && id <= 7) || id == 40 || id == 62 || id == 63 ||
        id == 467 || id == 468 || id == 469 || id == 193 || id == 194 ||
        id == 195 || id == 196 || (id >= 198 && id <= 199) ||
        (id >= 204 && id <= 209) ||
        (id >= 247 && id <= 259) || id == 71 || id == 72 || id == 73 ||
        id == 74 || id == 75 || id == 76 || id == 77 || id == 78 ||
        (id >= 118 && id <= 129) || (id >= 185 && id <= 192) ||
        (id >= 661 && id <= 695))
        return ObjectType::BLOCK;

    return ObjectType::DECORATION;
}

void LevelParser::computeHitbox(LevelObject& obj) {
    // Standard GD block size is 30x30 units
    switch (obj.type) {
        case ObjectType::BLOCK:
            obj.hitboxW = 30.0f;
            obj.hitboxH = 30.0f;
            break;
        case ObjectType::SPIKE:
            // Spike hitbox: 20x20 centered on spike position
            // Must be large enough that grounded player can't walk through
            obj.hitboxW = 20.0f;
            obj.hitboxH = 20.0f;
            break;
        case ObjectType::ORB:
            obj.hitboxW = 30.0f;
            obj.hitboxH = 30.0f;
            break;
        case ObjectType::PAD:
            obj.hitboxW = 30.0f;
            obj.hitboxH = 10.0f;
            break;
        default:
            obj.hitboxW = 30.0f;
            obj.hitboxH = 30.0f;
            break;
    }
    obj.hitboxW *= obj.scaleX;
    obj.hitboxH *= obj.scaleY;
}

void LevelParser::indexObjects(LevelData& level) {
    level.solids.clear();
    level.hazards.clear();
    level.orbs.clear();
    level.pads.clear();
    level.portals.clear();
    level.speedChanges.clear();

    float maxX = 0.0f;
    int shipPortals = 0, cubePortals = 0, ballPortals = 0, gravityPortals = 0;
    
    for (auto& obj : level.objects) {
        maxX = std::max(maxX, obj.x);
        switch (obj.type) {
            case ObjectType::BLOCK:
                level.solids.push_back(obj);
                break;
            case ObjectType::SPIKE:
                level.hazards.push_back(obj);
                break;
            case ObjectType::ORB:
                level.orbs.push_back(obj);
                break;
            case ObjectType::PAD:
                level.pads.push_back(obj);
                break;
            case ObjectType::PORTAL_SPEED:
                level.speedChanges.push_back(obj);
                break;
            case ObjectType::PORTAL_GRAVITY:
                level.portals.push_back(obj);
                gravityPortals++;
                break;
            case ObjectType::PORTAL_SHIP:
                level.portals.push_back(obj);
                shipPortals++;
                break;
            case ObjectType::PORTAL_BALL:
                level.portals.push_back(obj);
                ballPortals++;
                break;
            case ObjectType::PORTAL_UFO:
            case ObjectType::PORTAL_WAVE:
            case ObjectType::PORTAL_ROBOT:
            case ObjectType::PORTAL_SPIDER:
            case ObjectType::PORTAL_CUBE:
            case ObjectType::PORTAL_SWING:
                level.portals.push_back(obj);
                cubePortals++; // Count other gamemode portals
                break;
            default:
                break;
        }
    }
    level.totalLength = maxX + 100.0f;
    level.objectCount = (int)level.objects.size();
    
    // Debug: print portal counts
    std::cout << "[LevelParser] Portals: Ship=" << shipPortals 
              << " Cube=" << cubePortals << " Ball=" << ballPortals 
              << " Gravity=" << gravityPortals << std::endl;

    // Sort all indexed lists by X position for fast binary search during sim
    auto sortByX = [](std::vector<LevelObject>& v) {
        std::sort(v.begin(), v.end(),
            [](const LevelObject& a, const LevelObject& b) { return a.x < b.x; });
    };
    sortByX(level.solids);
    sortByX(level.hazards);
    sortByX(level.orbs);
    sortByX(level.pads);
    sortByX(level.portals);
    sortByX(level.speedChanges);
}

LevelData LevelParser::parseFromString(const std::string& levelString) {
    LevelData level;
    if (levelString.empty()) return level;

    // GD level format: header;obj1;obj2;...
    // Header and objects separated by ';'
    std::stringstream ss(levelString);
    std::string segment;
    bool firstSegment = true;

    while (std::getline(ss, segment, ';')) {
        if (segment.empty()) continue;

        if (firstSegment) {
            // First segment is the level header (settings)
            firstSegment = false;
            continue;
        }

        auto props = parseObjectProperties(segment);
        if (props.empty()) continue;

        LevelObject obj;
        // Key 1 = object ID
        if (props.count(1)) obj.id = std::stoi(props[1]);
        // Key 2 = X position
        if (props.count(2)) obj.x = std::stof(props[2]);
        // Key 3 = Y position
        if (props.count(3)) obj.y = std::stof(props[3]);
        // Key 6 = rotation
        if (props.count(6)) obj.rotation = std::stof(props[6]);
        // Key 32 = scale
        if (props.count(32)) {
            float s = std::stof(props[32]);
            obj.scaleX = s;
            obj.scaleY = s;
        }
        // Key 4 = flipX, Key 5 = flipY
        if (props.count(4)) obj.flipX = (props[4] == "1");
        if (props.count(5)) obj.flipY = (props[5] == "1");
        // Key 57 = group ID
        if (props.count(57)) obj.groupID = std::stoi(props[57]);

        obj.type = classifyObject(obj.id);
        computeHitbox(obj);

        // Adjust block Y to align with ground line - blocks appear sunken
        // Ground line is at y=15, block center needs to be at y=3 for top to align
        if (obj.type == ObjectType::BLOCK) {
            // Shift ALL blocks up by 12 units to align with ground line
            obj.y += 12.0f;
        }

        // Speed portal subtype
        if (obj.type == ObjectType::PORTAL_SPEED) {
            if (obj.id == 200) obj.speedType = SpeedType::HALF;
            else if (obj.id == 201) obj.speedType = SpeedType::NORMAL;
            else if (obj.id == 202) obj.speedType = SpeedType::DOUBLE;
            else if (obj.id == 203) obj.speedType = SpeedType::TRIPLE;
            else if (obj.id == 1334) obj.speedType = SpeedType::QUAD;
        }

        // Only add gameplay-relevant objects
        if (obj.type != ObjectType::DECORATION && obj.type != ObjectType::UNKNOWN) {
            level.objects.push_back(obj);
        }
    }

    indexObjects(level);
    
    // Add ceiling to prevent ship from flying over obstacles
    addCeilingToLevel(level, 250.0f);
    
    // Debug: print loaded object counts
    std::cout << "[LevelParser] Loaded level: " << level.objectCount << " objects" << std::endl;
    std::cout << "  Solids: " << level.solids.size() 
              << " | Hazards: " << level.hazards.size()
              << " | Orbs: " << level.orbs.size()
              << " | Pads: " << level.pads.size()
              << " | Portals: " << level.portals.size()
              << " | Speed: " << level.speedChanges.size() << std::endl;
    
    return level;
}

LevelData LevelParser::parseFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[LevelParser] Could not open: " << filepath << std::endl;
        return {};
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    // Try to decode (official levels are base64+gzip)
    std::string decoded = decodeOfficialLevel(content);
    if (!decoded.empty()) {
        std::cout << "[LevelParser] Decoded level from " << filepath
                  << " (" << decoded.size() << " chars)" << std::endl;
        return parseFromString(decoded);
    }

    // Maybe it's already plain text
    if (content.find(';') != std::string::npos) {
        return parseFromString(content);
    }

    std::cerr << "[LevelParser] Failed to decode level from: " << filepath << std::endl;
    return {};
}

LevelObject LevelParser::createCeilingBlock(float x, float y) {
    LevelObject obj;
    obj.id = 1;
    obj.x = x;
    obj.y = y;
    obj.type = ObjectType::BLOCK;
    obj.hitboxW = 30.0f;
    obj.hitboxH = 30.0f;
    return obj;
}

void LevelParser::addCeilingToLevel(LevelData& level, float defaultCeilingY) {
    // Find all ship portals and other gamemode portals
    std::vector<std::pair<float, bool>> portalEvents; // (x, isShipPortal)
    
    for (const auto& obj : level.portals) {
        if (obj.type == ObjectType::PORTAL_SHIP) {
            portalEvents.push_back({obj.x, true}); // Enter ship mode
        } else if (obj.type == ObjectType::PORTAL_CUBE || 
                   obj.type == ObjectType::PORTAL_BALL ||
                   obj.type == ObjectType::PORTAL_UFO ||
                   obj.type == ObjectType::PORTAL_WAVE ||
                   obj.type == ObjectType::PORTAL_ROBOT ||
                   obj.type == ObjectType::PORTAL_SPIDER ||
                   obj.type == ObjectType::PORTAL_SWING) {
            portalEvents.push_back({obj.x, false}); // Exit ship mode (enter other mode)
        }
    }
    
    // Sort by X position
    std::sort(portalEvents.begin(), portalEvents.end(), 
              [](const auto& a, const auto& b) { return a.first < b.first; });
    
    // Find ship sections (from ship portal to next non-ship portal)
    std::vector<std::pair<float, float>> shipSections; // (startX, endX)
    bool inShipSection = false;
    float shipStartX = 0.0f;
    
    for (const auto& event : portalEvents) {
        if (event.second && !inShipSection) {
            // Entering ship mode
            shipStartX = event.first;
            inShipSection = true;
        } else if (!event.second && inShipSection) {
            // Exiting ship mode (entering another mode)
            shipSections.push_back({shipStartX, event.first});
            inShipSection = false;
        }
    }
    
    // If still in ship mode at end, extend to end of level
    if (inShipSection) {
        float maxX = 0.0f;
        for (const auto& obj : level.objects) {
            maxX = std::max(maxX, obj.x);
        }
        shipSections.push_back({shipStartX, maxX + 100.0f});
    }
    
    // Find the highest block in each ship section
    const float BLOCK_SIZE = 30.0f;
    int ceilingBlocksAdded = 0;
    
    for (const auto& section : shipSections) {
        float startX = section.first;
        float endX = section.second;
        
        // Find highest block Y in this ship section
        float maxBlockY = 0.0f;
        for (const auto& obj : level.solids) {
            if (obj.x >= startX - 60.0f && obj.x <= endX + 60.0f) {
                // Block top = obj.y + hitboxH/2
                float blockTop = obj.y + obj.hitboxH * 0.5f;
                maxBlockY = std::max(maxBlockY, blockTop);
            }
        }
        
        // Ceiling at highest structure level + 1.5 blocks margin (reduced from 3)
        float ceilingY = (maxBlockY > 0.0f) ? maxBlockY : defaultCeilingY;
        ceilingY += 1.5f * BLOCK_SIZE; // 45 units margin
        
        int startBlock = static_cast<int>(startX / BLOCK_SIZE);
        int endBlock = static_cast<int>((endX + BLOCK_SIZE - 1) / BLOCK_SIZE);
        
        for (int i = startBlock; i <= endBlock; i++) {
            float x = i * BLOCK_SIZE;
            
            // Check if there's already a block at this position
            bool exists = false;
            for (const auto& obj : level.solids) {
                if (std::abs(obj.x - x) < BLOCK_SIZE * 0.5f && std::abs(obj.y - ceilingY) < BLOCK_SIZE * 0.5f) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                level.objects.push_back(createCeilingBlock(x, ceilingY));
                ceilingBlocksAdded++;
            }
        }
    }
    
    // Re-index to include the new ceiling blocks
    if (ceilingBlocksAdded > 0) {
        indexObjects(level);
        std::cout << "[LevelParser] Added " << ceilingBlocksAdded << " ceiling blocks in " 
                  << shipSections.size() << " ship section(s)" << std::endl;
    } else {
        std::cout << "[LevelParser] No ship sections found, no ceiling added" << std::endl;
    }
}

// ============================================================================
// Programmatic test level creation (no file needed)
// ============================================================================
LevelData LevelParser::createTestLevel(int difficulty) {
    LevelData level;
    level.name = "TestLevel_" + std::to_string(difficulty);
    float x = 0.0f;
    const float BLOCK = 30.0f;

    // Ground blocks from start to end
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

    // Build a ground floor
    int levelBlocks = 200 + difficulty * 100;
    for (int i = 0; i < levelBlocks; i++) {
        addBlock(i * BLOCK, 0.0f);         // Ground level
        addBlock(i * BLOCK, -BLOCK);       // Below ground (extra solid)
    }

    // Add obstacles based on difficulty
    // Ground top = 15 (blocks at y=0, height 30, top = 0+15=15)
    // Spike visual is 30 units tall, sits on ground. Hitbox is at the TIP (top 10 units).
    // Spike tip center = groundTop + 30 (visual height) - 5 (half hitbox) = 40
    float groundTop = 15.0f;
    // Player on ground: y=27.5, inner hitbox (0.8x) top = 27.5 + 25*0.4 = 37.5
    // Spike at y=35, hitbox 20x20 → deadly zone [25,45]
    // Grounded player inner top 37.5 IS inside [25,45] → DIES (must jump)
    // Jump peak y=87, inner bottom = 87 - 10 = 77 > 45 → CLEARS
    float spikeY = 35.0f;

    srand(42 + difficulty); // deterministic
    x = 10 * BLOCK; // start obstacles after some flat ground (more room to learn)
    while (x < (levelBlocks - 5) * BLOCK) {
        int pattern = rand() % (3 + difficulty);

        if (pattern == 0) {
            // Single spike
            addSpike(x, spikeY);
            x += 5 * BLOCK; // more spacing for easier learning
        }
        else if (pattern == 1) {
            // Double spike
            addSpike(x, spikeY);
            addSpike(x + BLOCK, spikeY);
            x += 6 * BLOCK;
        }
        else if (pattern == 2) {
            // Single spike with tighter spacing
            addSpike(x, spikeY);
            x += 4 * BLOCK;
        }
        else if (pattern == 3) {
            // Triple spike
            addSpike(x, spikeY);
            addSpike(x + BLOCK, spikeY);
            addSpike(x + 2 * BLOCK, spikeY);
            x += 7 * BLOCK;
        }
        else if (pattern == 4) {
            // Spike on raised platform
            addBlock(x, BLOCK);
            addBlock(x + BLOCK, BLOCK);
            addSpike(x + 2 * BLOCK, BLOCK + groundTop + 15.0f);
            x += 7 * BLOCK;
        }
        else {
            // Two spikes with gap between
            addSpike(x, spikeY);
            addSpike(x + 3 * BLOCK, spikeY);
            x += 6 * BLOCK;
        }
    }

    indexObjects(level);
    std::cout << "[LevelParser] Created test level '" << level.name
              << "' with " << level.objectCount << " objects, length="
              << level.totalLength << std::endl;
    return level;
}

// ============================================================================
// Tutorial level - single easy spike for initial learning
// ============================================================================
LevelData LevelParser::createTutorialLevel() {
    LevelData level;
    level.name = "Tutorial_SingleSpike";
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

    // Ground floor - 100 blocks = 3000 units
    for (int i = 0; i < 100; i++) {
        addBlock(i * BLOCK, 0.0f);
        addBlock(i * BLOCK, -BLOCK);
    }

    // Single spike at x=150 (5 blocks away) - easy to learn
    float spikeY = 35.0f;
    addSpike(5 * BLOCK, spikeY);  // First spike at x=150
    addSpike(10 * BLOCK, spikeY);  // x=300
    addSpike(15 * BLOCK, spikeY);  // x=450
    addSpike(20 * BLOCK, spikeY);  // x=600

    indexObjects(level);
    std::cout << "[LevelParser] Created tutorial level with "
              << level.objectCount << " objects, single easy spike pattern"
              << std::endl;
    return level;
}

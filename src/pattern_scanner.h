#pragma once

#include <Windows.h>
#include <vector>
#include <cstdint>
#include <string>
#include <iostream>

// ============================================================================
// Pattern Scanner for GD Memory Structures
// Dynamically finds offsets by scanning for known patterns
// ============================================================================

class PatternScanner {
public:
    static uintptr_t findGameManager(HANDLE process, uintptr_t baseAddr, size_t moduleSize);
    static uintptr_t findPlayLayer(HANDLE process, uintptr_t gameManager);
    static uintptr_t findPlayerObject(HANDLE process, uintptr_t playLayer);
    
    // Scan for pattern in memory
    static uintptr_t scanPattern(HANDLE process, uintptr_t start, size_t size, 
                                  const std::vector<uint8_t>& pattern,
                                  const std::vector<uint8_t>& mask);
    
private:
    static bool comparePattern(const uint8_t* data, const std::vector<uint8_t>& pattern,
                                  const std::vector<uint8_t>& mask);
};

// Alternative: Use value scanning to find player position
class ValueScanner {
public:
    // Find address where X coordinate is stored (player moves right)
    static uintptr_t findPlayerX(HANDLE process, uintptr_t start, size_t size);
    
    // Find address where Y coordinate is stored (player jumps up/down)
    static uintptr_t findPlayerY(HANDLE process, uintptr_t start, size_t size);
};

// ============================================================================
// AOB (Array of Bytes) Patterns for GD 2.2
// These patterns identify key structures in the game code
// ============================================================================

// GameManager singleton access pattern
// Typically looks for "GameManager sharedState()" call pattern
static const std::vector<uint8_t> GAME_MANAGER_PATTERN = {
    0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00,  // mov rax, [rip+offset]
    0x48, 0x8B, 0x40, 0x00,                     // mov rax, [rax+offset]
    0xC3                                         // ret
};
static const std::vector<uint8_t> GAME_MANAGER_MASK = {
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,  // First 3 bytes exact
    0xFF, 0xFF, 0xFF, 0x00,                     // Next 3 exact, 1 wild
    0xFF                                          // ret exact
};

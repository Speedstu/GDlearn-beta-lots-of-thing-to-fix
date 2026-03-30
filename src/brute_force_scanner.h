#pragma once

#include <Windows.h>
#include <cstdint>
#include <vector>
#include <functional>
#include <iostream>

// ============================================================================
// Brute force offset finder - tries ALL offsets in a range
// ============================================================================

class BruteForceScanner {
public:
    // Try all offsets from base to find one that produces a valid float
    static bool findFloatOffset(HANDLE process, uintptr_t base, int minOffset, int maxOffset, 
                                 float minValue, float maxValue, int& foundOffset);
    
    // Try all offsets to find one that produces a valid byte (0 or 1)
    static bool findBoolOffset(HANDLE process, uintptr_t base, int minOffset, int maxOffset,
                                int& foundOffset);
    
    // Verify an offset by reading it multiple times
    static bool verifyOffset(HANDLE process, uintptr_t base, int offset, 
                              std::function<bool(float)> validator, int samples = 5);
    
    // Scan for player structure by looking for X/Y pair
    static uintptr_t findPlayerByXYPair(HANDLE process, uintptr_t start, size_t size,
                                         float expectedX, float expectedY, float tolerance = 10.0f);
};

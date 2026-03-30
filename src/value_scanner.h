#pragma once

#include <Windows.h>
#include <cstdint>
#include <vector>
#include <functional>

// ============================================================================
// Direct memory value scanner - finds structures by their values
// ============================================================================

class ValueScanner {
public:
    // Scan for PlayLayer by looking for percentage value (0-100)
    // Call this when the player is at the beginning of a level (percent should be near 0)
    static uintptr_t findPlayLayerByPercent(HANDLE process, uintptr_t start, size_t size, float expectedPercent);
    
    // Scan for Player object by looking for X/Y position
    // Player typically starts at X=0-100 and Y around 100-200
    static uintptr_t findPlayerByPosition(HANDLE process, uintptr_t start, size_t size, float expectedX, float expectedY);
    
    // Generic float scanner
    static std::vector<uintptr_t> scanFloat(HANDLE process, uintptr_t start, size_t size, 
                                             float value, float tolerance = 0.1f);
    
    // Generic pointer scanner - finds pointers to a specific address
    static std::vector<uintptr_t> scanPointerTo(HANDLE process, uintptr_t start, size_t size, 
                                                 uintptr_t target);
};

#include "brute_force_scanner.h"
#include <cmath>

bool BruteForceScanner::findFloatOffset(HANDLE process, uintptr_t base, int minOffset, int maxOffset,
                                           float minValue, float maxValue, int& foundOffset) {
    std::cout << "[BruteForce] Scanning for float offset in range [" << minOffset << ", " << maxOffset << "]..." << std::endl;
    
    for (int offset = minOffset; offset <= maxOffset; offset += 4) {
        float value;
        SIZE_T read;
        if (ReadProcessMemory(process, (LPCVOID)(base + offset), &value, sizeof(value), &read)) {
            if (value >= minValue && value <= maxValue && value == value) { // not NaN
                // Verify it's consistent
                bool consistent = true;
                for (int i = 0; i < 3; i++) {
                    float check;
                    ReadProcessMemory(process, (LPCVOID)(base + offset), &check, sizeof(check), &read);
                    if (std::abs(check - value) > 0.1f) {
                        consistent = false;
                        break;
                    }
                }
                if (consistent) {
                    foundOffset = offset;
                    std::cout << "[BruteForce] Found float offset: 0x" << std::hex << offset 
                              << " -> " << std::dec << value << std::endl;
                    return true;
                }
            }
        }
    }
    return false;
}

bool BruteForceScanner::findBoolOffset(HANDLE process, uintptr_t base, int minOffset, int maxOffset,
                                        int& foundOffset) {
    std::cout << "[BruteForce] Scanning for bool offset in range [" << minOffset << ", " << maxOffset << "]..." << std::endl;
    
    for (int offset = minOffset; offset <= maxOffset; offset += 1) {
        uint8_t value;
        SIZE_T read;
        if (ReadProcessMemory(process, (LPCVOID)(base + offset), &value, sizeof(value), &read)) {
            if (value == 0 || value == 1) {
                foundOffset = offset;
                std::cout << "[BruteForce] Found bool offset: 0x" << std::hex << offset 
                          << " -> " << std::dec << (int)value << std::endl;
                return true;
            }
        }
    }
    return false;
}

bool BruteForceScanner::verifyOffset(HANDLE process, uintptr_t base, int offset,
                                      std::function<bool(float)> validator, int samples) {
    for (int i = 0; i < samples; i++) {
        float value;
        SIZE_T read;
        if (!ReadProcessMemory(process, (LPCVOID)(base + offset), &value, sizeof(value), &read)) {
            return false;
        }
        if (!validator(value)) {
            return false;
        }
    }
    return true;
}

uintptr_t BruteForceScanner::findPlayerByXYPair(HANDLE process, uintptr_t start, size_t size,
                                                  float expectedX, float expectedY, float tolerance) {
    std::cout << "[BruteForce] Scanning for Player by X/Y pair (X=" << expectedX << ", Y=" << expectedY << ")..." << std::endl;
    
    std::vector<uint8_t> buffer(4096);
    
    for (uintptr_t addr = start; addr < start + size; addr += 4096 - sizeof(float)) {
        SIZE_T bytesRead;
        if (!ReadProcessMemory(process, (LPCVOID)addr, buffer.data(), buffer.size(), &bytesRead)) {
            continue;
        }
        
        for (size_t i = 0; i <= bytesRead - 2 * sizeof(float); i += sizeof(float)) {
            float x = *(float*)&buffer[i];
            float y = *(float*)&buffer[i + sizeof(float)];
            
            // Check if X and Y match expected values within tolerance
            if (std::abs(x - expectedX) <= tolerance && std::abs(y - expectedY) <= tolerance) {
                // Found potential Player - return the base address (X offset - 0x67C)
                uintptr_t player = addr + i - 0x67C;
                std::cout << "[BruteForce] Found Player at: 0x" << std::hex << player 
                          << " (X=" << x << ", Y=" << y << ")" << std::dec << std::endl;
                return player;
            }
        }
    }
    
    return 0;
}
